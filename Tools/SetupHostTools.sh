#!/usr/bin/env bash
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="${SCRIPT_DIR}/.."
HOST_TOOLS="${ROOT_DIR}/_Build/HostTools"
# Tools used
MD5COMMAND="md5sum"

#platform
SC_HOST_ARCHITECTURE=""
case $(uname -m) in
    i386)   SC_HOST_ARCHITECTURE="386" ;;
    i686)   SC_HOST_ARCHITECTURE="386" ;;
    x86_64) SC_HOST_ARCHITECTURE="amd64" ;;
    arm)    dpkg --print-architecture | grep -q "arm64" && SC_HOST_ARCHITECTURE="arm64" || SC_HOST_ARCHITECTURE="arm" ;;
esac
echo "SC_HOST_ARCHITECTURE=${SC_HOST_ARCHITECTURE}"

# options
SC_CLEANUP_SOURCE_PACKAGE=false

cd "${ROOT_DIR}"
mkdir -p "$HOST_TOOLS"

COLOR_RED="\033[31m"
COLOR_GREEN="\033[32m"
COLOR_NONE="\033[00m"

# Find local binary packages directory
[ -z ${SC_BINARIES_DIR} ] && SC_BINARIES_DIR="${HOME}/.sc_binaries"
echo "SC_BINARIES_DIR = ${SC_BINARIES_DIR}"

# Create Binaries dir
if [ ! -d ${SC_BINARIES_DIR} ]; then
    mkdir -p "${SC_BINARIES_DIR}" || { echo -e "${COLOR_RED}FATAL ERROR: Cannot create ${SC_BINARIES_DIR}"; exit 1; }
fi

# -------------------------------------------------------------------------------------------------------------------
# GIT 
# -------------------------------------------------------------------------------------------------------------------
git --version 2>&1 >/dev/null
GIT_EXISTS=$?
[ $GIT_EXISTS -eq 0 ] || { echo -e "${COLOR_RED}FATAL ERROR: git doesn't exist, please install it on your system${COLOR_NONE}" ; exit 1; }

SC_GIT_URL=$(git config --get remote.origin.url) || { echo -e "${COLOR_RED}FATAL ERROR: git remote url failed${COLOR_NONE}" ; exit 1; }
echo SC_GIT_URL=${SC_GIT_URL}
# -------------------------------------------------------------------------------------------------------------------
# GIT-LFS 
# -------------------------------------------------------------------------------------------------------------------
git-lfs --version 2>&1 >/dev/null
GIT_LFS_EXISTS=$?

if [ $GIT_LFS_EXISTS -eq 0 ] ; then
echo -e "Tool ${COLOR_GREEN}'git-lfs'${COLOR_NONE} exists"
else

SC_PACKAGE_NAME="git-lfs"
if [ "${SC_HOST_ARCHITECTURE}" == "amd64" ]; then
SC_PACKAGE_VERSION="git-lfs-darwin-amd64-v3.3.0"
SC_PACKAGE_MD5="025808a443a2575ad7ded3640053acdf"
else
SC_PACKAGE_VERSION="git-lfs-darwin-arm64-v3.3.0"
SC_PACKAGE_MD5="e63a5ca5734c2c506eb8118379229f47"
fi
SC_PACKAGE_URL="https://github.com/git-lfs/git-lfs/releases/download/v3.3.0/${SC_PACKAGE_VERSION}.zip"
SC_PACKAGE_LOCAL_FILE="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_VERSION}.zip"
SC_PACKAGE_LOCAL_TXT="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_VERSION}.txt"
SC_PACKAGE_LOCAL_DIR="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_VERSION}"
if [ -f ${SC_PACKAGE_LOCAL_TXT} ]; then
    SC_PACKAGE_INSTALLED=true
else
    SC_PACKAGE_INSTALLED=false
fi

if [ "$SC_PACKAGE_INSTALLED" = true ]; then
    echo -e "Package ${COLOR_GREEN}'${SC_PACKAGE_NAME}'${COLOR_NONE} is installed in version '${SC_PACKAGE_VERSION}'"
else
    echo -e "Package  ${COLOR_GREEN}'${SC_PACKAGE_NAME}'${COLOR_NONE} is missing, installing it in version '${SC_PACKAGE_VERSION}'"
    mkdir -p "${SC_PACKAGE_LOCAL_DIR}" || { echo -e "${COLOR_RED}FATAL ERROR: Cannot create directory ${SC_PACKAGE_LOCAL_DIR} ${COLOR_NONE}" ; exit 1; }
    SC_PACKAGE_DOWNLOADED_MD5=""
    if [ -e "$SC_PACKAGE_LOCAL_FILE" ]; then
        # md5="$(md5sum ${SC_PACKAGE_LOCAL_FILE} | cut -f1 -d' ')"
        SC_PACKAGE_DOWNLOADED_MD5="$(md5 ${SC_PACKAGE_LOCAL_FILE} | rev | cut -f1 -d' ' | rev)"
        echo "${SC_PACKAGE_NAME} SC_PACKAGE_DOWNLOADED_MD5='$SC_PACKAGE_DOWNLOADED_MD5'"
    fi
    if [ "$SC_PACKAGE_DOWNLOADED_MD5" != ${SC_PACKAGE_MD5} ] ; then
        rm -f ${SC_PACKAGE_LOCAL_FILE}
        curl -L -o "${SC_PACKAGE_LOCAL_FILE}" "${SC_PACKAGE_URL}" || { echo -e "${COLOR_RED}FATAL ERROR: Cannot download ${SC_PACKAGE_URL} to ${SC_PACKAGE_LOCAL_FILE} ${COLOR_NONE}" ; exit 1; }
        # md5="$(md5sum ${SC_PACKAGE_LOCAL_FILE} | cut -f1 -d' ')"
        SC_PACKAGE_DOWNLOADED_MD5="$(md5 ${SC_PACKAGE_LOCAL_FILE} | rev | cut -f1 -d' ' | rev)"
        
        if [ "${SC_PACKAGE_DOWNLOADED_MD5}" != ${SC_PACKAGE_MD5} ] ; then
            echo -e "${COLOR_RED}FATAL ERROR: Uncorract md5 for ${SC_PACKAGE_URL} to ${SC_PACKAGE_LOCAL_FILE} (${SC_PACKAGE_DOWNLOADED_MD5}) ${COLOR_NONE}"
            exit 1
        fi
    else
        echo "Skipped downloading ${SC_PACKAGE_NAME}"
    fi
    # tar -xvf "${SC_PACKAGE_LOCAL_FILE}" -C "${SC_PACKAGE_LOCAL_DIR}" --strip-components=1 || { echo -e "${COLOR_RED}FATAL ERROR: Cannot untar ${SC_PACKAGE_LOCAL_FILE} ${COLOR_NONE}" ; exit 1; }
    unzip "${SC_PACKAGE_LOCAL_FILE}" -d  "${SC_PACKAGE_LOCAL_DIR}"  || { echo -e "${COLOR_RED}FATAL ERROR: Cannot unzip ${SC_PACKAGE_LOCAL_FILE} ${COLOR_NONE}" ; exit 1; }
    if [ "$SC_CLEANUP_SOURCE_PACKAGE" = true ]; then
        rm -f ${SC_PACKAGE_LOCAL_FILE}
    fi
    xattr -r -d com.apple.quarantine "${SC_PACKAGE_LOCAL_DIR}"/* || { echo -e "${COLOR_RED}FATAL ERROR: Cannot remove quarantine attr on ${SC_PACKAGE_LOCAL_DIR} ${COLOR_NONE}" ; exit 1; }
    printf "SC_PACKAGE_URL=${SC_PACKAGE_URL}\nSC_PACKAGE_MD5=${SC_PACKAGE_MD5}\n" > "${SC_PACKAGE_LOCAL_TXT}"
fi

# Install git-lfs
"${SC_PACKAGE_LOCAL_DIR}/git-lfs-3.3.0/install.sh" || { echo -e "${COLOR_RED}FATAL ERROR: git-lfs cannot install ${COLOR_NONE}" ; exit 1; }

git-lfs --version 2>&1 >/dev/null
GIT_LFS_EXISTS=$?

[ $GIT_LFS_EXISTS -eq 0 ] || { echo -e "${COLOR_RED}FATAL ERROR: git-lfs was installed but it doesn't exist${COLOR_NONE}" ; exit 1; }

fi

# -------------------------------------------------------------------------------------------------------------------
# CLANG 
# -------------------------------------------------------------------------------------------------------------------

SC_PACKAGE_NAME="clang"
if [ "${SC_HOST_ARCHITECTURE}" == "amd64" ]; then
SC_PACKAGE_VERSION="clang+llvm-15.0.3-x86_64-apple-darwin"
SC_PACKAGE_MD5="c25d35ee6a51419ed61bc306ea41a1cc"
else
SC_PACKAGE_VERSION="clang+llvm-15.0.3-arm64-apple-darwin21.0"
SC_PACKAGE_MD5="082cb0945be9e8ed9ae75954ac9b4d82"
fi
SC_PACKAGE_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-15.0.3/${SC_PACKAGE_VERSION}.tar.xz"
SC_PACKAGE_LOCAL_FILE="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_VERSION}.tar.xz"
SC_PACKAGE_LOCAL_TXT="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_VERSION}.txt"
SC_PACKAGE_LOCAL_DIR="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_VERSION}"
if [ -f ${SC_PACKAGE_LOCAL_TXT} ]; then
    SC_PACKAGE_INSTALLED=true
else
    SC_PACKAGE_INSTALLED=false
fi

if [ "$SC_PACKAGE_INSTALLED" = true ]; then
    echo -e "Package ${COLOR_GREEN}'${SC_PACKAGE_NAME}'${COLOR_NONE} is installed in version '${SC_PACKAGE_VERSION}'"
else
    echo -e "Package  ${COLOR_GREEN}'${SC_PACKAGE_NAME}'${COLOR_NONE} is missing, installing it in version '${SC_PACKAGE_VERSION}'"
    mkdir -p "${SC_PACKAGE_LOCAL_DIR}" || { echo -e "${COLOR_RED}FATAL ERROR: Cannot create directory ${SC_PACKAGE_LOCAL_DIR} ${COLOR_NONE}" ; exit 1; }
    SC_PACKAGE_DOWNLOADED_MD5=""
    if [ -e "$SC_PACKAGE_LOCAL_FILE" ]; then
        # md5="$(md5sum ${SC_PACKAGE_LOCAL_FILE} | cut -f1 -d' ')"
        SC_PACKAGE_DOWNLOADED_MD5="$(md5 ${SC_PACKAGE_LOCAL_FILE} | rev | cut -f1 -d' ' | rev)"
        echo "${SC_PACKAGE_NAME} SC_PACKAGE_DOWNLOADED_MD5='$SC_PACKAGE_DOWNLOADED_MD5'"
    fi
    if [ "$SC_PACKAGE_DOWNLOADED_MD5" != ${SC_PACKAGE_MD5} ] ; then
        rm -f ${SC_PACKAGE_LOCAL_FILE}
        curl -L -o "${SC_PACKAGE_LOCAL_FILE}" "${SC_PACKAGE_URL}" || { echo -e "${COLOR_RED}FATAL ERROR: Cannot download ${SC_PACKAGE_URL} to ${SC_PACKAGE_LOCAL_FILE} ${COLOR_NONE}" ; exit 1; }
        # md5="$(md5sum ${SC_PACKAGE_LOCAL_FILE} | cut -f1 -d' ')"
        SC_PACKAGE_DOWNLOADED_MD5="$(md5 ${SC_PACKAGE_LOCAL_FILE} | rev | cut -f1 -d' ' | rev)"
        
        if [ "${SC_PACKAGE_DOWNLOADED_MD5}" != ${SC_PACKAGE_MD5} ] ; then
            echo -e "${COLOR_RED}FATAL ERROR: Uncorract md5 for ${SC_PACKAGE_URL} to ${SC_PACKAGE_LOCAL_FILE} (${SC_PACKAGE_DOWNLOADED_MD5}) ${COLOR_NONE}"
            exit 1
        fi
    else
        echo "Skipped downloading ${SC_PACKAGE_NAME}"
    fi
    tar -xvf "${SC_PACKAGE_LOCAL_FILE}" -C "${SC_PACKAGE_LOCAL_DIR}" --strip-components=1 || { echo -e "${COLOR_RED}FATAL ERROR: Cannot untar ${SC_PACKAGE_LOCAL_FILE} ${COLOR_NONE}" ; exit 1; }
    if [ "$SC_CLEANUP_SOURCE_PACKAGE" = true ]; then
        rm -f ${SC_PACKAGE_LOCAL_FILE}
    fi
    xattr -r -d com.apple.quarantine "${SC_PACKAGE_LOCAL_DIR}"/* || { echo -e "${COLOR_RED}FATAL ERROR: Cannot remove quarantine attr on ${SC_PACKAGE_LOCAL_DIR} ${COLOR_NONE}" ; exit 1; }
    printf "SC_PACKAGE_URL=${SC_PACKAGE_URL}\nSC_PACKAGE_MD5=${SC_PACKAGE_MD5}\n" > "${SC_PACKAGE_LOCAL_TXT}"
fi

# Remove link
if [ -d "${HOST_TOOLS}/${SC_PACKAGE_NAME}" ]; then
    rm "${HOST_TOOLS}/${SC_PACKAGE_NAME}"
fi
# Create Link
ln -s "${SC_PACKAGE_LOCAL_DIR}" "${HOST_TOOLS}/${SC_PACKAGE_NAME}" 

# Test the tools
if [ "$(echo "int    asd=0;" | "${HOST_TOOLS}/${SC_PACKAGE_NAME}/bin/clang-format")" != "int asd = 0;" ]; then
    echo -e "${COLOR_RED}FATAL ERROR: clang-format doesn't work inside ${HOST_TOOLS}/${SC_PACKAGE_NAME} ${COLOR_NONE}" ; exit 1;
fi

# -------------------------------------------------------------------------------------------------------------------
# EMSDK 
# -------------------------------------------------------------------------------------------------------------------

SC_PACKAGE_NAME="emsdk"
SC_PACKAGE_VERSION="3.1.28"
SC_PACKAGE_FILE_NAME="${SC_PACKAGE_NAME}_${SC_PACKAGE_VERSION}"
SC_PACKAGE_URL="https://github.com/emscripten-core/emsdk.git"
SC_PACKAGE_LOCAL_TXT="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_FILE_NAME}.txt"
SC_PACKAGE_LOCAL_DIR="${SC_BINARIES_DIR}/${SC_PACKAGE_NAME}/${SC_PACKAGE_FILE_NAME}"

if [ -f ${SC_PACKAGE_LOCAL_TXT} ]; then
    SC_PACKAGE_INSTALLED=true
else
    SC_PACKAGE_INSTALLED=false
fi

if [ "$SC_PACKAGE_INSTALLED" = true ]; then
    echo -e "Package ${COLOR_GREEN}'${SC_PACKAGE_NAME}'${COLOR_NONE} is installed in version '${SC_PACKAGE_FILE_NAME}'"
else
    echo -e "Package  ${COLOR_GREEN}'${SC_PACKAGE_NAME}'${COLOR_NONE} is missing, installing it in version '${SC_PACKAGE_FILE_NAME}'"
    rm -rf "${SC_PACKAGE_LOCAL_DIR}"
    mkdir -p "${SC_PACKAGE_LOCAL_DIR}"
    git clone --branch ${SC_PACKAGE_VERSION} "${SC_PACKAGE_URL}" "${SC_PACKAGE_LOCAL_DIR}"
    ${SC_PACKAGE_LOCAL_DIR}/emsdk install ${SC_PACKAGE_VERSION}
    ${SC_PACKAGE_LOCAL_DIR}/emsdk activate ${SC_PACKAGE_VERSION}
    printf "SC_PACKAGE_URL=${SC_PACKAGE_URL}\n" > "${SC_PACKAGE_LOCAL_TXT}"
fi

# Remove link
if [ -d "${HOST_TOOLS}/${SC_PACKAGE_NAME}" ]; then
    rm "${HOST_TOOLS}/${SC_PACKAGE_NAME}"
fi
# Create Link
ln -s "${SC_PACKAGE_LOCAL_DIR}" "${HOST_TOOLS}/${SC_PACKAGE_NAME}" 
