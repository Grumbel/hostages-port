# SPDX-FileCopyrightText: 2025–2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Android APK via NDK + SDL2 (same pipeline family as kugelmatch/pingus).
#
#   nix build .#hostages-android
#   nix run .#hostages-android-install

{ pkgs
, sdlSrc
, sdlVersion
, androidSdk
, buildToolsVersion
, packagePlatform
, compilePlatform
, targetAbis
, version
, versionCode
}:

let
  abis = pkgs.lib.concatStringsSep " " targetAbis;

  topAndroidMk = pkgs.writeText "Android.mk" "include $(call all-subdir-makefiles)\n";

  applicationMk = pkgs.writeText "Application.mk" ''
    APP_STL := c++_shared
    APP_ABI := ${abis}
    APP_PLATFORM := android-${packagePlatform}
  '';

  sdlAndroidLibs = pkgs.stdenvNoCC.mkDerivation {
    pname = "sdl2-android-libs";
    version = sdlVersion;
    dontUnpack = true;
    nativeBuildInputs = [ androidSdk pkgs.jdk17 pkgs.gnumake ];
    env = {
      BUILD_TOOLS_VERSION = buildToolsVersion;
      COMPILE_PLATFORM = compilePlatform;
      PACKAGE_PLATFORM = packagePlatform;
      SDL_SRC = "${sdlSrc}";
      APPLICATION_MK = applicationMk;
      TOP_ANDROID_MK = topAndroidMk;
    };
    buildPhase = ''
      runHook preBuild
      export ANDROID_HOME=${androidSdk}/libexec/android-sdk
      bash ${../mk/android/scripts}/build-sdl-libs.sh
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      TARGET_ABIS=${pkgs.lib.escapeShellArg abis} bash ${../mk/android/scripts}/install-sdl-libs.sh
      runHook postInstall
    '';
    dontPatchELF = true;
    dontStrip = true;
  };

  sdlPrebuiltMk = pkgs.writeText "SDL2-prebuilt-Android.mk" ''
    LOCAL_PATH := $(call my-dir)
    include $(CLEAR_VARS)
    LOCAL_MODULE := SDL2
    LOCAL_SRC_FILES := ${sdlAndroidLibs}/lib/$(TARGET_ARCH_ABI)/libSDL2.so
    include $(PREBUILT_SHARED_LIBRARY)
  '';

  gameSrc = pkgs.lib.cleanSource ../.;

  apkName = "hostages-${version}.apk";

  apk = pkgs.stdenvNoCC.mkDerivation {
    pname = "hostages-android";
    inherit version;
    dontUnpack = true;
    nativeBuildInputs = [ androidSdk pkgs.jdk17 pkgs.zip pkgs.gnumake ];
    env = {
      BUILD_TOOLS_VERSION = buildToolsVersion;
      PACKAGE_PLATFORM = packagePlatform;
      TARGET_ABIS = abis;
      APP_NAME = "hostages";
      APP_DIR = "${../mk/android/app}";
      GAME_SRC_DIR = "${gameSrc}";
      APPLICATION_MK = applicationMk;
      TOP_ANDROID_MK = topAndroidMk;
      SDL_PREBUILT_MK = sdlPrebuiltMk;
      SDL_ANDROID_LIBS = sdlAndroidLibs;
      KEYSTORE = "${../mk/android/keystore/debug.keystore}";
      GAME_VERSION = version;
      VERSION_CODE = toString versionCode;
    };
    buildPhase = ''
      runHook preBuild
      export ANDROID_HOME=${androidSdk}/libexec/android-sdk
      bash ${../mk/android/scripts}/build-apk.sh
      runHook postBuild
    '';
    installPhase = ''
      mkdir -p $out
      cp out/hostages.apk $out/${apkName}
    '';
    dontPatchELF = true;
    dontStrip = true;
    meta = {
      description = "Hostages Android APK";
      license = pkgs.lib.licenses.gpl3Plus;
      platforms = pkgs.lib.platforms.linux;
      hydraPlatforms = [];
    };
  };

  installApp = {
    type = "app";
    program = toString (pkgs.writeShellScript "install-hostages-apk" ''
      exec ${pkgs.android-tools}/bin/adb install -r ${apk}/${apkName}
    '');
    meta.description = "Install the Hostages APK on a connected Android device with adb";
  };

in {
  inherit sdlAndroidLibs apk installApp;
}
