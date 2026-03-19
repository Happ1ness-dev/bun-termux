# Troubleshooting Bun-Termux

## `ConnectionRefused`

This usually happens when Bun fails to find resolv.conf file, due to missing resolv-conf package or symlink.
Creating/linking the file in glibc prefix should resolve the error. [Source.](https://github.com/Happ1ness-dev/bun-termux/issues/1#issuecomment-4012897662)

Install resolv-conf and link it (recommended):
```bash
pkg install resolv-conf

ln -sf $PREFIX/usr/etc/resolv.conf $PREFIX/usr/glibc/etc/resolv.conf
```

Or just create a file:
```bash
cat <<EOF > /data/data/com.termux/files/usr/glibc/etc/resolv.conf
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF
```

## `bad interpreter: No such file or directory`

This usually happens when the shebang is pointing to an incorrect location (e.g. `#!/usr/bin/env node` instead of `#!/data/data/com.termux/files/usr/bin/env node`).
Bun-Termux's shim already attempts to intercept and redirect these, but not everything is running under shim.
In which case `termux-fix-shebang problematic_shebang_file.js` should help.
[Source.](https://github.com/Happ1ness-dev/bun-termux/issues/1#issuecomment-4014030481)

## `EACCES: Permission denied while installing ...`

This happens when bun is trying to hardlink modules.
The shim should intercept this on newer versions and force bun to copy files instead, but older versions of the shim didn't do that, in which case you can try setting `BUN_OPTIONS="--backend=copyfile"`.

Or you could just update to the latest version of bun-termux:
```bash
# in bun-termux repo
git pull
make clean install
```
