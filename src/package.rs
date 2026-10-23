use std::env;
use std::process::Command;

use serde_json::Value;

use crate::channel::Channel;

const ENV_FLAKE: &str = "NIX_SEARCH_META_FLAKE";
const ENV_SYSTEM: &str = "NIX_SEARCH_META_SYSTEM";
const ENV_NAME: &str = "NIX_SEARCH_META_NAME";

#[derive(Debug, Default)]
pub struct PackageInfo {
    pub name: String,
    pub full_name: String,
    pub version: String,
    pub description: String,
    pub build_inputs: Vec<String>,
    pub native_inputs: Vec<String>,
    pub propagated_inputs: Vec<String>,
}

/*
 * The package name is not interpolated into this expression. It is read
 * from the environment with builtins.getEnv, so quotes, dots and other
 * Nix syntax cannot be injected by the query.
 */
const META_EXPR: &str = r#"
let
  flakeRef = builtins.getEnv "NIX_SEARCH_META_FLAKE";
  system = builtins.getEnv "NIX_SEARCH_META_SYSTEM";
  name = builtins.getEnv "NIX_SEARCH_META_NAME";
  flake = builtins.getFlake flakeRef;
  legacy = flake.legacyPackages.${system} or {};
  packages = flake.packages.${system} or {};
  parts = builtins.filter builtins.isString (builtins.split "\\." name);
  pick = set: parts:
    if parts == [] then set
    else if !(builtins.isAttrs set) then null
    else
      let
        head = builtins.head parts;
        tail = builtins.tail parts;
      in
        if builtins.hasAttr head set
        then pick (builtins.getAttr head set) tail
        else null;
  fromLegacy = pick legacy parts;
  fromPackages = pick packages parts;
  pkg = if fromLegacy != null then fromLegacy else fromPackages;
  depNames = list:
    builtins.filter (x: x != null) (builtins.map (d:
      if builtins.isAttrs d && d ? name then d.name
      else if builtins.isAttrs d && d ? pname then d.pname
      else null) (if builtins.isList list then list else []));
in
if pkg == null || !(builtins.isAttrs pkg) || (pkg.type or "") != "derivation"
then { found = false; }
else {
  found = true;
  name = pkg.pname or (pkg.name or name);
  fullName = pkg.name or "";
  version = pkg.version or "";
  description = pkg.meta.description or "";
  buildInputs = depNames (pkg.buildInputs or []);
  nativeBuildInputs = depNames (pkg.nativeBuildInputs or []);
  propagatedBuildInputs = depNames (pkg.propagatedBuildInputs or []);
}
"#;

fn nix_bin() -> String {
    env::var("NIX_SEARCH_NIX")
        .ok()
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| "nix".to_string())
}

pub fn validate_name(name: &str) -> Result<(), String> {
    if name.is_empty() {
        return Err("пустое имя пакета".to_string());
    }
    if name.len() > 512 {
        return Err("слишком длинное имя пакета".to_string());
    }

    let mut component_len = 0usize;
    for character in name.chars() {
        if character == '.' {
            if component_len == 0 {
                return Err("некорректное имя пакета: пустая часть между точками".to_string());
            }
            component_len = 0;
        } else {
            if character.is_whitespace() || character.is_control() {
                return Err("некорректный символ в имени пакета".to_string());
            }
            component_len += 1;
            if component_len > 128 {
                return Err("слишком длинная часть имени пакета".to_string());
            }
        }
    }

    if component_len == 0 {
        return Err("некорректное имя пакета: пустая часть между точками".to_string());
    }
    Ok(())
}

fn string_field(json: &Value, key: &str, fallback: &str) -> String {
    json.get(key)
        .and_then(Value::as_str)
        .filter(|value| !value.is_empty())
        .unwrap_or(fallback)
        .to_string()
}

fn string_array(json: &Value, key: &str) -> Vec<String> {
    json.get(key)
        .and_then(Value::as_array)
        .map(|array| {
            array
                .iter()
                .filter_map(Value::as_str)
                .map(str::to_string)
                .collect()
        })
        .unwrap_or_default()
}

pub fn lookup(channel: &Channel, system: &str, name: &str) -> Result<Option<PackageInfo>, String> {
    let output = Command::new(nix_bin())
        .arg("--extra-experimental-features")
        .arg("nix-command flakes")
        .arg("eval")
        .arg("--quiet")
        .arg("--impure")
        .arg("--json")
        .arg("--no-warn-dirty")
        .arg("--expr")
        .arg(META_EXPR)
        .env(ENV_FLAKE, &channel.reference)
        .env(ENV_SYSTEM, system)
        .env(ENV_NAME, name)
        .output()
        .map_err(|error| format!("не удалось запустить nix: {error}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        let message = stderr.trim();
        return Err(if message.is_empty() {
            "nix eval завершился с ошибкой".to_string()
        } else {
            message.to_string()
        });
    }

    let json: Value = serde_json::from_slice(&output.stdout)
        .map_err(|error| format!("не удалось разобрать JSON от nix: {error}"))?;

    match json.get("found").and_then(Value::as_bool) {
        Some(false) => Ok(None),
        Some(true) => Ok(Some(PackageInfo {
            name: string_field(&json, "name", name),
            full_name: string_field(&json, "fullName", ""),
            version: string_field(&json, "version", ""),
            description: string_field(&json, "description", ""),
            build_inputs: string_array(&json, "buildInputs"),
            native_inputs: string_array(&json, "nativeBuildInputs"),
            propagated_inputs: string_array(&json, "propagatedBuildInputs"),
        })),
        _ => Err("nix eval вернул неожиданный JSON".to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn valid_package_names() {
        for name in [
            "hello",
            "python3Packages.requests",
            "nodePackages.@angular/cli",
            "_1password",
            "foo+bar",
        ] {
            assert!(validate_name(name).is_ok(), "{name}");
        }
    }

    #[test]
    fn invalid_package_names() {
        for name in ["", ".", ".foo", "foo.", "foo..bar", "foo bar", "foo\nbar"] {
            assert!(validate_name(name).is_err(), "{name}");
        }

        let long_component = "a".repeat(129);
        assert!(validate_name(&long_component).is_err());
    }
}
