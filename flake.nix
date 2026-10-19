{
  description = "HyprView — a macOS Mission Control-style overview plugin for Hyprland";

  inputs = {
    # Pin a tagged Hyprland release, not `main`: ABI must match the compositor, and main's
    # dep tree is intermittently unbuildable (e.g. a missing pango buildInput in
    # hyprland-guiutils broke it on 2026-06-27). v0.55.4 is the version this was developed +
    # proven against (`nix build .#hyprview` succeeds on real NixOS). Downstream should set
    # `inputs.hyprland.follows` to build the plugin against THEIR exact Hyprland.
    hyprland.url = "github:hyprwm/Hyprland?ref=v0.55.4";
    nixpkgs.follows = "hyprland/nixpkgs";
    systems.follows = "hyprland/systems";
  };

  outputs = {
    self,
    hyprland,
    nixpkgs,
    systems,
    ...
  }: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);
    pkgsFor = eachSystem (system: import nixpkgs {inherit system;});
  in {
    packages = eachSystem (system: let
      pkgs = pkgsFor.${system};
      hyprlandPkg = hyprland.packages.${system}.hyprland;
    in {
      # mkHyprlandPlugin now lives in nixpkgs (pkgs.hyprlandPlugins.mkHyprlandPlugin), not in
      # the Hyprland flake's `lib`. It is built on hyprland.stdenv.mkDerivation and auto-adds
      # pkg-config + hyprland + hyprland.buildInputs. We `.override` the whole scope so BOTH
      # the build stdenv and the hyprland buildInput are the EXACT pinned Hyprland (ABI must
      # match the running compositor), not nixpkgs' possibly-skewed hyprland.
      hyprview = (pkgs.hyprlandPlugins.override {hyprland = hyprlandPkg;}).mkHyprlandPlugin {
        pluginName = "hyprview";
        version = "0.3.0";
        src = ./.;

        nativeBuildInputs = [pkgs.cmake pkgs.pkg-config];
        # Hyprland's own build inputs are propagated by mkHyprlandPlugin; we only add Lua
        # (for the hyprview.* Lua config functions). luajit is what Hyprland links.
        buildInputs = [pkgs.luajit];

        # The build emits `hyprview.so` (CMake PREFIX ""), but Home Manager's
        # `wayland.windowManager.hyprland.plugins = [ pkg ]` looks for `lib<pname>.so`.
        # Symlink it so the idiomatic one-liner install works with no extra config.
        postInstall = ''
          ln -sf hyprview.so "$out/lib/libhyprview.so"
        '';

        meta = {
          description = "macOS Mission Control-style overview for Hyprland";
          homepage = "https://github.com/blackcityskyline/hyprview";
          license = lib.licenses.gpl3Plus;
          platforms = lib.platforms.linux;
        };
      };

      default = self.packages.${system}.hyprview;
    });

    devShells = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in {
      default = pkgs.mkShell {
        # `nix develop` gives a shell that can configure+build the plugin against the
        # pinned Hyprland (cmake -S . -B build && cmake --build build).
        inputsFrom = [self.packages.${system}.hyprview];
        packages = [pkgs.clang-tools];
      };
    });

    formatter = eachSystem (system: pkgsFor.${system}.alejandra);
  };
}
