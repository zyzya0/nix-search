use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[allow(dead_code)] // extra fields are useful for diagnostics and tests
#[derive(Debug, Clone, Default)]
pub struct Channel {
    pub reference: String,
    pub branch: String,
    pub version: String,
    pub source: String,
    pub local_path: Option<PathBuf>,
    pub local: bool,
}

fn nix_bin() -> String {
    env::var("NIX_SEARCH_NIX")
        .ok()
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| "nix".to_string())
}

fn nix_instantiate_bin() -> String {
    env::var("NIX_SEARCH_NIX_INSTANTIATE")
        .ok()
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| "nix-instantiate".to_string())
}

fn run_capture(program: &str, args: &[&str]) -> Option<(String, String, bool)> {
    let output = Command::new(program).args(args).output().ok()?;
    Some((
        String::from_utf8_lossy(&output.stdout).into_owned(),
        String::from_utf8_lossy(&output.stderr).into_owned(),
        output.status.success(),
    ))
}

fn parse_major_minor(text: &str) -> Option<(u32, u32)> {
    let bytes = text.as_bytes();
    let start = bytes.iter().position(|byte| byte.is_ascii_digit())?;
    let mut pos = start;
    while pos < bytes.len() && bytes[pos].is_ascii_digit() {
        pos += 1;
    }
    if pos >= bytes.len() || bytes[pos] != b'.' {
        return None;
    }
    let major = text[start..pos].parse::<u32>().ok()?;

    pos += 1;
    let minor_start = pos;
    while pos < bytes.len() && bytes[pos].is_ascii_digit() {
        pos += 1;
    }
    if pos == minor_start {
        return None;
    }
    let minor = text[minor_start..pos].parse::<u32>().ok()?;
    Some((major, minor))
}

pub fn branch_from_version(version: &str) -> Option<String> {
    let (major, minor) = parse_major_minor(version)?;
    if version.contains("pre") || version.contains("unstable") {
        return Some("nixos-unstable".to_string());
    }
    Some(format!("nixos-{major}.{minor:02}"))
}

pub fn branch_from_name(name: &str) -> Option<String> {
    if name.contains("unstable") || name.contains("master") {
        return Some("nixos-unstable".to_string());
    }
    let (major, minor) = parse_major_minor(name)?;
    if name.contains("pre") {
        return Some("nixos-unstable".to_string());
    }
    Some(format!("nixos-{major}.{minor:02}"))
}

fn version_from_branch(branch: &str) -> String {
    if branch.contains("unstable") {
        return "unstable".to_string();
    }
    match parse_major_minor(branch) {
        Some((major, minor)) => format!("{major}.{minor:02}"),
        None => String::new(),
    }
}

fn branch_channel(branch: &str, source: &str) -> Channel {
    Channel {
        reference: format!("github:NixOS/nixpkgs/{branch}"),
        branch: branch.to_string(),
        version: version_from_branch(branch),
        source: source.to_string(),
        local_path: None,
        local: false,
    }
}

fn local_channel(path: &Path, source: &str) -> Channel {
    let resolved = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    let version = fs::read_to_string(resolved.join(".version"))
        .or_else(|_| fs::read_to_string(resolved.join("lib/.version")))
        .map(|text| text.trim().to_string())
        .unwrap_or_default();
    let branch = branch_from_version(&version).unwrap_or_else(|| "nixos-unstable".to_string());
    let has_flake = resolved.join("flake.nix").exists();
    let reference = if has_flake {
        format!("path:{}", resolved.display())
    } else {
        format!("github:NixOS/nixpkgs/{branch}")
    };

    Channel {
        reference,
        branch,
        version,
        source: source.to_string(),
        local_path: Some(resolved),
        local: true,
    }
}

fn detect_from_find_file() -> Option<Channel> {
    let tool = nix_instantiate_bin();
    let (stdout, _stderr, ok) = run_capture(&tool, &["--find-file", "nixpkgs"])?;
    if !ok {
        return None;
    }
    let path = stdout.trim();
    if path.starts_with('/') && Path::new(path).exists() {
        Some(local_channel(Path::new(path), "local nixpkgs"))
    } else {
        None
    }
}

fn detect_from_home_channel() -> Option<Channel> {
    let home = env::var_os("HOME")?;
    let path = PathBuf::from(home).join(".nix-defexpr/channels/nixpkgs");
    if path.exists() {
        Some(local_channel(&path, "user nix channel"))
    } else {
        None
    }
}

fn detect_from_registry() -> Option<Channel> {
    let (stdout, _stderr, ok) = run_capture(&nix_bin(), &["registry", "list"])?;
    if !ok {
        return None;
    }

    for line in stdout.lines() {
        let Some(pos) = line.find("flake:nixpkgs ") else {
            continue;
        };
        let value = line[pos + "flake:nixpkgs ".len()..].trim();
        if value.is_empty() || value == "flake:nixpkgs" {
            continue;
        }

        if let Some(path) = value.strip_prefix("path:") {
            let clean = path.split(|c| c == '?' || c == '#').next().unwrap_or(path);
            return Some(local_channel(Path::new(clean), "nixpkgs registry"));
        }
        if value.starts_with('/') {
            return Some(local_channel(Path::new(value), "nixpkgs registry"));
        }
        if let Some(tail) = value.strip_prefix("github:NixOS/nixpkgs/") {
            let branch = tail.split(|c| c == '?' || c == '#').next().unwrap_or(tail);
            if !branch.is_empty() {
                return Some(branch_channel(branch, "nixpkgs registry"));
            }
        }
        if value == "github:NixOS/nixpkgs" || value == "flake:nixpkgs" {
            return Some(branch_channel("nixos-unstable", "nixpkgs registry"));
        }
    }

    None
}

fn detect_from_channels() -> Option<Channel> {
    let (stdout, _stderr, ok) = run_capture("nix-channel", &["--list"])?;
    if !ok {
        return None;
    }

    let mut nixos_url: Option<String> = None;
    let mut nixpkgs_url: Option<String> = None;
    for line in stdout.lines() {
        let mut parts = line.split_whitespace();
        let Some(name) = parts.next() else {
            continue;
        };
        let Some(url) = parts.next() else {
            continue;
        };
        if name == "nixpkgs" {
            nixpkgs_url = Some(url.to_string());
        } else if name == "nixos" {
            nixos_url = Some(url.to_string());
        }
    }

    let url = nixpkgs_url.as_deref().or(nixos_url.as_deref())?;
    let base = url.trim_end_matches('/').rsplit('/').next().unwrap_or(url);
    let branch = branch_from_name(base)?;
    Some(branch_channel(&branch, "nix channel"))
}

fn os_release_value(text: &str, key: &str) -> Option<String> {
    let prefix = format!("{key}=");
    for line in text.lines() {
        let Some(value) = line.strip_prefix(&prefix) else {
            continue;
        };
        let value = value.trim();
        let value = value
            .strip_prefix('"')
            .and_then(|rest| rest.strip_suffix('"'))
            .unwrap_or(value);
        return Some(value.to_string());
    }
    None
}

fn detect_from_os_release() -> Option<Channel> {
    let text = fs::read_to_string("/etc/os-release").ok()?;
    if !text.contains("ID=nixos") {
        return None;
    }

    let version_id = os_release_value(&text, "VERSION_ID");
    let build_id = os_release_value(&text, "BUILD_ID");
    let version_text = os_release_value(&text, "VERSION");

    let unstable = [&version_id, &build_id, &version_text]
        .into_iter()
        .flatten()
        .any(|value| value.contains("pre") || value.contains("unstable"));
    if unstable {
        return Some(branch_channel("nixos-unstable", "NixOS release"));
    }

    let version = version_id.or(build_id).or(version_text)?;
    let branch = branch_from_name(&version)?;
    Some(branch_channel(&branch, "NixOS release"))
}

fn detect_from_nixos_version() -> Option<Channel> {
    let (stdout, _stderr, ok) = run_capture("nixos-version", &[])?;
    if !ok {
        return None;
    }
    let branch = branch_from_name(stdout.trim())?;
    Some(branch_channel(&branch, "NixOS release"))
}

pub fn detect() -> Result<Channel, String> {
    if let Some(channel) = detect_from_find_file() {
        return Ok(channel);
    }
    if let Some(channel) = detect_from_home_channel() {
        return Ok(channel);
    }
    if let Some(channel) = detect_from_registry() {
        return Ok(channel);
    }
    if let Some(channel) = detect_from_channels() {
        return Ok(channel);
    }
    if let Some(channel) = detect_from_os_release() {
        return Ok(channel);
    }
    if let Some(channel) = detect_from_nixos_version() {
        return Ok(channel);
    }

    Ok(branch_channel(
        "nixos-unstable",
        "fallback (no channel detected)",
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_map_to_branches() {
        assert_eq!(
            branch_from_version("26.11"),
            Some("nixos-26.11".to_string())
        );
        assert_eq!(
            branch_from_version("26.11.20261020.02c087f"),
            Some("nixos-26.11".to_string())
        );
        assert_eq!(
            branch_from_version("26.11pre-git"),
            Some("nixos-unstable".to_string())
        );
        assert_eq!(
            branch_from_version("26.11.20261020.02c087f"),
            Some("nixos-26.11".to_string())
        );
    }

    #[test]
    fn names_map_to_branches() {
        assert_eq!(
            branch_from_name("nixos-unstable"),
            Some("nixos-unstable".to_string())
        );
        assert_eq!(
            branch_from_name("nixpkgs-25.05-darwin"),
            Some("nixos-25.05".to_string())
        );
        assert_eq!(
            branch_from_name("https://nixos.org/channels/nixos-25.05"),
            Some("nixos-25.05".to_string())
        );
        assert_eq!(
            branch_from_name("https://nixos.org/channels/nixpkgs-unstable"),
            Some("nixos-unstable".to_string())
        );
        assert_eq!(branch_from_name("wat"), None);
    }

    #[test]
    fn branch_versions_are_formatted() {
        assert_eq!(version_from_branch("nixos-26.11"), "26.11");
        assert_eq!(version_from_branch("nixos-25.05"), "25.05");
        assert_eq!(version_from_branch("nixos-unstable"), "unstable");
    }

    #[test]
    fn local_channel_uses_flake_path() {
        let dir = env::temp_dir().join(format!("nix-search-rust-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("flake.nix"), "{}").unwrap();
        fs::write(dir.join(".version"), "26.11pre-git\n").unwrap();

        let channel = local_channel(&dir, "test");
        assert!(channel.local);
        assert_eq!(channel.version, "26.11pre-git");
        assert_eq!(channel.branch, "nixos-unstable");
        assert!(channel.reference.starts_with("path:"));
        let _ = fs::remove_dir_all(&dir);
    }
}
