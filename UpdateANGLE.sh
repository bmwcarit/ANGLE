set -e

find . -mindepth 1 ! -regex '^./.git\(/.*\)*' -a ! -regex '^./UpdateANGLE.sh' -delete

git clone https://github.com/WebKit/WebKit.git WebKit
cd WebKit
WEBKITCOMMIT=$(git rev-parse HEAD)
cd ..

cp -r ./WebKit/Source/ThirdParty/ANGLE/* ./
cp -r ./WebKit/Source/ThirdParty/ANGLE/.[^.]* ./
rm -rf WebKit

# Remove files with potentially incompatible licenses
rm ./scripts/generate_android_bp.py
rm ./scripts/roll_chromium_deps.py
rm -rf ./tools/android_system_sdk/
rm -rf ./third_party/proguard/
rm -rf ./tools/flex-bison/
rm -rf ./third_party/android_system_sdk/
rm -rf ./util/windows/third_party/StackWalker
rm -rf ./third_party/glmark2/
rm -rf ./third_party/jdk/

# ADD README
README=$(cat <<EOF
# Fork of ANGLE

This is a fork of [WebKit\'s version of ANGLE](https://github.com/WebKit/WebKit) for use in the iOS Version of [RAMSES](https://github.com/bmwcarit/ramses).
Makes the repository smaller and therefore faster to checkout.

To update this repository to the most recent commit run \`./UpdateANGLE.sh\`.

$(cat ./README.md)
EOF
)
echo "$README" > ./README.md

git add --all
git add -f third_party/zlib
git commit -m "Updated ANGLE to WebKit commit $WEBKITCOMMIT"