#!/bin/env bash
set -e

if [ ! -e "src/api/api.h" ]; then
  echo "Please run this script from the root directory of Lite GL."
  exit 1
fi

source scripts/common.sh

ARCH="$(uname -m)"
BUILD_DIR="$(get_default_build_dir)"
RUN_BUILD=true
STATIC_BUILD=false
ADDONS=false
BUILD_TYPE="debug"

unset pgo

show_help(){
  echo
  echo "Usage: $0 <OPTIONS>"
  echo
  echo "Available options:"
  echo
  echo "-h --help                 Show this help and exits."
  echo "-b --builddir DIRNAME     Sets the name of the build dir (no path)."
  echo "                          Default: '${BUILD_DIR}'."
  echo "   --debug                Debug this script."
  echo "-n --nobuild              Skips the build step, use existing files."
  echo "-s --static               Specify if building using static libraries."
  echo "-v --version VERSION      Specify a version, non whitespace separated string."
  echo "-a --addons               Install 3rd party addons."
  echo "-r --release              Compile in release mode."
  echo "-O --pgo                  Use profile guided optimizations (pgo)."
  echo
}

initial_arg_count=$#

for i in "$@"; do
  case $i in
    -h|--help)
      show_help
      exit 0
      ;;
    -b|--builddir)
      BUILD_DIR="$2"
      shift
      shift
      ;;
    -a|--addons)
      ADDONS=true
      shift
      ;;
    --debug)
      set -x
      shift
      ;;
    -n|--nobuild)
      RUN_BUILD=false
      shift
      ;;
    -r|--release)
      BUILD_TYPE="release"
      shift
      ;;
    -O|--pgo)
      pgo="-Db_pgo=generate"
      shift
      ;;
    -s|--static)
      STATIC_BUILD=true
      shift
      ;;
    -v|--version)
      VERSION="$2"
      shift
      shift
      ;;
    *)
      # unknown option
      ;;
  esac
done

# show help if no valid argument was found
if [ $initial_arg_count -eq $# ]; then
  show_help
  exit 1
fi

setup_appimagetool() {
  if [ ! -e appimagetool ]; then
    if ! wget -O appimagetool "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${ARCH}.AppImage" ; then
      echo "Could not download the appimagetool for the arch '${ARCH}'."
      exit 1
    else
      chmod 0755 appimagetool
    fi
  fi
}

download_appimage_apprun() {
  if [ ! -e AppRun ]; then
    if ! wget -O AppRun "https://github.com/AppImage/AppImageKit/releases/download/continuous/AppRun-${ARCH}" ; then
      echo "Could not download AppRun for the arch '${ARCH}'."
      exit 1
    else
      chmod 0755 AppRun
    fi
  fi
}

build_litegl() {
  if [ -e build ]; then
    rm -rf build
  fi

  if [ -e ${BUILD_DIR} ]; then
    rm -rf ${BUILD_DIR}
  fi

  echo "Build lite-gl..."
  sleep 1
  if [[ $STATIC_BUILD == false ]]; then
    meson setup --buildtype=$BUILD_TYPE --prefix=/usr $pgo ${BUILD_DIR}
  else
    meson setup --wrap-mode=forcefallback \
      --buildtype=$BUILD_TYPE \
      --prefix=/usr \
      $pgo \
      ${BUILD_DIR}
  fi
  meson compile -C ${BUILD_DIR}
  if [[ -n $pgo ]]; then
    cp -r data "${BUILD_DIR}/src"
    "${BUILD_DIR}/src/lite-gl"
    meson configure -Db_pgo=use "${BUILD_DIR}"
    meson compile -C "${BUILD_DIR}"
    rm -fr "${BUILD_DIR}/data"
  fi
}

generate_appimage() {
  if [ -e LiteGL.AppDir ]; then
    rm -rf LiteGL.AppDir
  fi

  echo "Creating LiteGL.AppDir..."

  DESTDIR="$(realpath LiteGL.AppDir)" meson install --skip-subprojects -C ${BUILD_DIR}
  mv AppRun LiteGL.AppDir/
  # These could be symlinks but it seems they doesn't work with AppimageLauncher
  cp resources/icons/lite-gl.svg LiteGL.AppDir/
  cp resources/linux/org.lite_gl.lite_gl.desktop LiteGL.AppDir/

  if [[ $ADDONS == true ]]; then
    addons_download "${BUILD_DIR}"
    addons_install "${BUILD_DIR}" "LiteGL.AppDir/usr/share/lite-gl"
  fi

  if [[ $STATIC_BUILD == false ]]; then
    echo "Copying libraries..."

    mkdir -p LiteGL.AppDir/usr/lib/

    local allowed_libs=(
      libfreetype
      libpcre2
      libSDL2
      libsndio
      liblua
    )

    while read line; do
      local libname="$(echo $line | cut -d' ' -f1)"
      local libpath="$(echo $line | cut -d' ' -f2)"
      for lib in "${allowed_libs[@]}" ; do
        if echo "$libname" | grep "$lib" > /dev/null ; then
          cp "$libpath" LiteGL.AppDir/usr/lib/
          continue 2
        fi
      done
      echo "  Ignoring: $libname"
    done < <(ldd build/src/lite-gl | awk '{print $1 " " $3}')
  fi

  echo "Generating AppImage..."
  local version=""
  if [ -n "$VERSION" ]; then
    version="-$VERSION"
  fi

  if [[ $ADDONS == true ]]; then
    version="${version}-addons"
  fi

  ./appimagetool --appimage-extract-and-run LiteGL.AppDir LiteGL${version}-${ARCH}.AppImage
}

setup_appimagetool
download_appimage_apprun
if [[ $RUN_BUILD == true ]]; then build_litegl; fi
generate_appimage $1
