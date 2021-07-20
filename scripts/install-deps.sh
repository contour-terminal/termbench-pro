#! /bin/bash

set -e

install_deps_ubuntu()
{
    local RELEASE=`lsb_release -r | awk '{print $NF}'`

    local packages=(
        build-essential
        cmake
        g++
        make
    )

    if [[ "${RELEASE}" < "19.04" ]]; then
        packages=( ${packages[*]} g++-8 )
    fi

    apt install ${packages[*]}
}

main_linux()
{
    local ID=`lsb_release --id | awk '{print $NF}'`

    case "${ID}" in
        Ubuntu)
            install_deps_ubuntu
            ;;
        *)
            echo "No automated installation of build dependencies available yet."
            ;;
    esac
}

main_darwin()
{
    # brew install cmake clang?
    return
}

main()
{
    case "$OSTYPE" in
        linux-gnu*)
            main_linux
            ;;
        darwin*)
            main_darwin
            ;;
        *)
            echo "OS not supported."
            ;;
    esac
}

main
