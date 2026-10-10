{
  description = "hyblock - plugin for Hyprland which block top corner for float windows";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland?ref=v0.56.1";
    nixpkgs.follows = "hyprland/nixpkgs";
    systems.follows = "hyprland/systems";
  };

  outputs =
    {
      self,
      hyprland,
      nixpkgs,
      systems,
      ...
    }:
    let
      inherit (nixpkgs) lib;
      eachSystem = lib.genAttrs (import systems);
      pkgsFor = eachSystem (system: import nixpkgs { inherit system; });

      mkDeps = pkgs: {
        nativeBuildInputs = [
          pkgs.cmake
          pkgs.pkg-config
        ];
        buildInputs = [
          pkgs.luajit
        ];
      };
    in
    {
      packages = eachSystem (
        system:
        let
          pkgs = pkgsFor.${system};
          deps = mkDeps pkgs;
          # Mirror nixpkgs' patch: relax glaze constraint so the build does not
          # fall back to FetchContent (git clone) inside the sandbox.
          hyprlandPkg = hyprland.packages.${system}.hyprland.overrideAttrs (old: {
            postPatch = (old.postPatch or "") + ''
              substituteInPlace CMakeLists.txt --replace-fail "glaze 7...<8" "glaze"
            '';
          });
        in
        {
          hyblock = (pkgs.hyprlandPlugins.override { hyprland = hyprlandPkg; }).mkHyprlandPlugin {
            pluginName = "hyblock";
            version = "0.1.0";
            src = ./.;

            cmakeBuildType = "RelWithDebInfo";
            dontStrip = true;

            inherit (deps) nativeBuildInputs buildInputs;

            meta = {
              description = "Plugin for Hyprland which block top corner for float windows";
              platforms = lib.platforms.linux;
            };
          };

          default = self.packages.${system}.hyblock;
        }
      );

      devShells = eachSystem (
        system:
        let
          pkgs = pkgsFor.${system};
          deps = mkDeps pkgs;
        in
        {
          default = pkgs.mkShell {
            # Build: cmake -DCMAKE_BUILD_TYPE=Release -B build-cmake && cmake --build build-cmake -j"$(nproc)"
            inputsFrom = [ self.packages.${system}.hyblock ];
            packages = [ pkgs.clang-tools ];
            nativeBuildInputs = deps.nativeBuildInputs;
            buildInputs = deps.buildInputs;
          };
        }
      );
    };
}
