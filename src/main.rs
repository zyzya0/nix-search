mod channel;
mod package;

use std::env;
use std::process::ExitCode;

const VERSION: &str = env!("CARGO_PKG_VERSION");

fn usage() -> &'static str {
    "Usage: nix-search PACKAGE\n\
     \n\
     Show information about PACKAGE in the nixpkgs channel used by\n\
     this system.\n\
     \n\
     PACKAGE can be a plain name (hello) or an attribute path\n\
     (python3Packages.requests).\n\
     \n\
     Output:\n\
       package metadata and dependencies, or\n\
       \"такого пакета нет\" when the package does not exist.\n\
     \n\
     Exit status: 0 if found, 1 if not found, 2 on error.\n\
     \n\
     Options:\n\
       -h, --help     show this help\n\
       -V, --version  show version\n"
}

fn get_system() -> String {
    let arch = env::consts::ARCH;
    let os = env::consts::OS;

    match os {
        "macos" => {
            if arch == "aarch64" {
                "aarch64-darwin".to_string()
            } else {
                "x86_64-darwin".to_string()
            }
        }
        "linux" => match arch {
            "x86_64" => "x86_64-linux".to_string(),
            "aarch64" => "aarch64-linux".to_string(),
            "x86" => "i686-linux".to_string(),
            "arm" => "armv7l-linux".to_string(),
            other => format!("{other}-linux"),
        },
        other => format!("{arch}-{other}"),
    }
}

fn print_dependency_group(label: &str, items: &[String]) {
    if items.is_empty() {
        println!("  {label}: (нет)");
        return;
    }

    println!("  {label} ({}):", items.len());
    for item in items {
        println!("    - {item}");
    }
}

fn print_package_info(info: &package::PackageInfo) {
    println!("Пакет:       {}", info.name);
    if !info.full_name.is_empty() {
        println!("Полное имя:  {}", info.full_name);
    }
    let version = if info.version.is_empty() {
        "неизвестно"
    } else {
        &info.version
    };
    let description = if info.description.is_empty() {
        "нет описания"
    } else {
        &info.description
    };
    println!("Версия:      {version}");
    println!("Описание:    {description}");
    println!("\nЗависимости:");
    print_dependency_group("build", &info.build_inputs);
    print_dependency_group("native", &info.native_inputs);
    print_dependency_group("propagated", &info.propagated_inputs);
}

fn run(query: &str) -> ExitCode {
    if let Err(error) = package::validate_name(query) {
        eprintln!("nix-search: {error}");
        return ExitCode::from(2);
    }

    let channel = match channel::detect() {
        Ok(channel) => channel,
        Err(error) => {
            eprintln!("nix-search: {error}");
            return ExitCode::from(2);
        }
    };

    let system = get_system();
    match package::lookup(&channel, &system, query) {
        Ok(Some(info)) => {
            print_package_info(&info);
            ExitCode::SUCCESS
        }
        Ok(None) => {
            println!("такого пакета нет");
            ExitCode::from(1)
        }
        Err(error) => {
            eprintln!("nix-search: {error}");
            ExitCode::from(2)
        }
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();

    match args.as_slice() {
        [flag] if flag == "-h" || flag == "--help" => {
            print!("{}", usage());
            ExitCode::SUCCESS
        }
        [flag] if flag == "-V" || flag == "--version" => {
            println!("nix-search {VERSION}");
            ExitCode::SUCCESS
        }
        [query] => run(query),
        _ => {
            eprint!("{}", usage());
            ExitCode::from(2)
        }
    }
}
