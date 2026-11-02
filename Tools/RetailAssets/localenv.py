"""Local (non-Docker) backends for the Esteria Retail import tools.

The upstream tools reach the world database and the server's active DBC files
through the development environment's containers (`docker exec ac-database`,
`docker exec ac-worldserver`). On a machine that runs AzerothCore natively this
module provides the same two facilities from local processes and files:

  * the world database, through the local MySQL client, using the credentials
    already present in worldserver.conf (never printed, copied into a pack or
    written to command output)
  * the active server DBC providers, read from the server's data directory

Backend selection: set ESTERIA_DB_BACKEND=local to require this backend, or
=docker to require the upstream one. When the variable is unset the local
backend is used only if the `docker` executable is not on PATH.

Every path is overridable by environment variable so the same checkout works on
another machine without editing code:

  ESTERIA_SERVER          server installation root (default C:\\Acore2\\Server)
  ESTERIA_WORLDSERVER_CONF worldserver.conf (default <server>/configs/worldserver.conf)
  ESTERIA_SERVER_DATA     server data directory (default <server>/data)
  ESTERIA_MYSQL           local MySQL client (default C:\\Acore2\\DB\\mysql\\bin\\mysql.exe)
"""
from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

DEFAULT_SERVER = Path(r"C:\Acore2\Server")
DEFAULTS = {
    "ESTERIA_SERVER": DEFAULT_SERVER,
    "ESTERIA_WORLDSERVER_CONF": DEFAULT_SERVER / "configs" / "worldserver.conf",
    "ESTERIA_SERVER_DATA": DEFAULT_SERVER / "data",
    "ESTERIA_MYSQL": Path(r"C:\Acore2\DB\mysql\bin\mysql.exe"),
}
# Logical database -> the worldserver.conf key that describes it.
CONFIG_KEYS = {"WorldDatabaseInfo": "acore_world",
               "CharacterDatabaseInfo": "acore_characters",
               "LoginDatabaseInfo": "acore_auth"}

_databases = None


def setting(name):
    return Path(os.environ.get(name, DEFAULTS[name]))


def docker_available():
    return shutil.which("docker") is not None


def use_local():
    choice = os.environ.get("ESTERIA_DB_BACKEND", "").strip().lower()
    if choice in {"local", "native"}:
        return True
    if choice == "docker":
        return False
    return not docker_available()


def databases():
    """host/port/user/password/database for each logical database."""
    global _databases
    if _databases is not None:
        return _databases
    path = setting("ESTERIA_WORLDSERVER_CONF")
    if not path.is_file():
        raise ValueError(f"worldserver configuration not found: {path}")
    found = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key, value = key.strip(), value.strip().strip('"')
        # First occurrence wins; the shipped .conf.dist samples appear later in the file.
        if key not in CONFIG_KEYS or key in found:
            continue
        fields = value.split(";")
        if len(fields) != 5:
            raise ValueError(f"Unsupported {key} layout in {path}")
        host, port, user, password, database = fields
        if not (host and port.isdigit() and user and database):
            raise ValueError(f"Incomplete {key} in {path}")
        found[key] = {"host": host, "port": port, "user": user,
                      "password": password, "database": database}
    missing = sorted(set(CONFIG_KEYS) - set(found))
    if missing:
        raise ValueError(f"Missing database settings {missing} in {path}")
    _databases = found
    return found


def mysql(sql, database="acore_world"):
    """Run SQL through the local MySQL client; credentials travel by environment only."""
    client = setting("ESTERIA_MYSQL")
    if not client.is_file():
        raise ValueError(f"Local MySQL client not found: {client}")
    entries = databases()
    chosen = next((entry for entry in entries.values() if entry["database"] == database), None)
    if chosen is None:
        chosen = entries["WorldDatabaseInfo"]
    environment = dict(os.environ, MYSQL_PWD=chosen["password"])
    command = [str(client), "--host=" + chosen["host"], "--port=" + chosen["port"],
               "--user=" + chosen["user"], "--batch", "--skip-column-names",
               "--default-character-set=utf8mb4", "--database=" + database]
    result = subprocess.run(command, input=sql, text=True, encoding="utf-8",
                            capture_output=True, env=environment)
    if result.returncode != 0:
        detail = (result.stderr or "").strip().splitlines()
        raise ValueError("MySQL query failed: " + (detail[-1] if detail else f"exit {result.returncode}"))
    return result.stdout.strip()
