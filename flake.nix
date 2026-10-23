{
  description = "Show package info from the nixpkgs channel used by the system";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forAllSystems (pkgs: {
        default = pkgs.rustPlatform.buildRustPackage {
          pname = "nix-search";
          version = "1.1.0";

          src = ./.;
          cargoLock.lockFile = ./Cargo.lock;

          nativeBuildInputs = [ pkgs.makeWrapper ];

          doCheck = true;
          installPhase = ''
            install -Dm755 target/release/nix-search $out/bin/nix-search
            wrapProgram $out/bin/nix-search \
              --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.nix ]}
          '';

          meta = {
            description = "Show package info from the nixpkgs channel used by the system";
            license = pkgs.lib.licenses.mit;
            mainProgram = "nix-search";
            platforms = pkgs.lib.platforms.unix;
          };
        };
      });

      apps = forAllSystems (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.default}/bin/nix-search";
        };
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = [ pkgs.cargo pkgs.rustc ];
        };
      });
    };
}
