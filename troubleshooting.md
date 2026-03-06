# Troubleshooting Bun-Termux

## ConnectionRefused

This usually happens when Bun fails to find resolv.conf file.
Creating the file in glibc prefix should resolve the error. [Source.](https://github.com/Happ1ness-dev/bun-termux/issues/1#issuecomment-4012897662)

```bash
cat <<EOF > /data/data/com.termux/files/usr/glibc/etc/resolv.conf
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF
```

## bad interpreter: No such file or directory

Usually happens when the shebang is pointing to an incorrect location (e.g. `#!/usr/bin/env node` instead of `#!/data/data/com.termux/files/usr/bin/env node`).
Bun-Termux's shim already attempts to intercept and redirect these, but not everything is running under shim.
In which case `termux-fix-shebang problematic_shebang_file.js` should help.
[Source.](https://github.com/Happ1ness-dev/bun-termux/issues/1#issuecomment-4014030481)