# Running Drivebase with Bun-Termux

## Prerequisites

- Bun and Bun-Termux installed (See [Quick Start](../../../README.md#quick-start))
- PostgreSQL installed (`pg_ctl`, `psql`)
- Redis installed (`redis-server`, `redis-cli`)
- Node.js for the Drivebase

## Step 1: Install Requirements

```bash
pkg install postgresql redis nodejs

# install pgvector extension for PostgreSQL, required by Drivebase
cd $TMPDIR
git clone -b v0.8.2 https://github.com/pgvector/pgvector.git
cd pgvector
make install SHLIB_LINK="-lm"

# Clone Drivebase
cd $HOME # or any directory you want
git clone https://github.com/drivebase/drivebase
cd drivebase
```

---

## Step 2: Initialize PostgreSQL

```bash
# In drivebase repo for convenience (I'm lazy)

initdb -D pgdata --locale=C.UTF-8 --encoding=UTF8
```

---

## Step 3: Start PostgreSQL

```bash
pg_ctl -D pgdata -l pgdata/server.log start

pg_ctl -D pgdata status
```

To stop PostgreSQL later:
```bash
pg_ctl -D pgdata stop
```

---

## Step 4: Create Database and User

```bash
createdb -h localhost -U $(whoami) drivebase

psql -h localhost -U $(whoami) -d postgres -c "CREATE USER postgres WITH SUPERUSER PASSWORD 'postgres';"
```

---

## Step 5: Start Redis

```bash
# --ignore-warnings ARM64-COW-BUG is required on Termux ARM64
redis-server --port 6379 --daemonize yes --ignore-warnings ARM64-COW-BUG

redis-cli ping
# Expected output: PONG
```

To stop Redis later:
```bash
redis-cli shutdown
```

---

## Step 6: Setup Drivebase

```bash
# --verbose is required for node-gyp because... Android
BUN_OPTIONS="--os=android --verbose" GYP_DEFINES="android_ndk_path=''" bun install bufferutil

# install linux only packages too
bun install

cp .env.example .env
bun run generate
bun db:migrate
```

---

## Step 7: Run Drivebase

Turborepo doesn't like android, so we need a preload script.
```bash
# Assuming you've cloned `bun-termux` to Termux home
NODE_OPTIONS="--require $HOME/bun-termux/docs/other_projects/drivebase/preload-turbo-android.js" bun --env-file=.env dev
```

---

## Consecutive Runs

After initial setup, to restart everything:

```bash
# PostgreSQL
pg_ctl -D pgdata -l pgdata/server.log start

# Redis
redis-server --port 6379 --daemonize yes --ignore-warnings ARM64-COW-BUG

# Verify (optional)
pg_ctl -D pgdata status && redis-cli ping

# Drivebase
NODE_OPTIONS="--require $HOME/bun-termux/docs/other_projects/drivebase/preload-turbo-android.js" bun --env-file=.env dev

# Stop
redis-cli shutdown; pg_ctl -D pgdata stop
```

---

## Notes

- `pgvector` extension must be compiled and installed from source because it's not available in termux packages.
- **Authentication**: This setup uses "trust" mode for local development. Do not use in production.
- **Data persistence**: Database files are stored in `pgdata`.
- `preload-turbo-android.js` preload is required to make `turbo` treat Android as Linux.
