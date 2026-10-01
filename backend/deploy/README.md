<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Unified backup and childcare service

The unified image replaces only the existing `cloud-backup` service. It keeps
Cloud Backup 0.3.2 intact, including authentication, migrations, versioned
objects, quotas, restore tools and configured Skill downloads. Both FastAPI
applications run in one process; startup, maintenance and shutdown are forwarded
to both. Each retains its database and credential model. This is service and
entry-point integration, not a conversion of childcare records into file blobs.

## Compatible entry points

The existing Cockpit Nginx route strips `/cloud-backup/` and forwards to
`cloud-backup:8080`. No change to its port mapping or Nginx is needed:

| External path after the existing host and port | Purpose |
| --- | --- |
| `/cloud-backup/api/v1/...` | Existing file API; unchanged |
| `/cloud-backup/admin/v1/...` | Existing backup administration and Skill exports; unchanged |
| `/cloud-backup/admin/` | Redirect to the shared workspace |
| `/cloud-backup/console` | Full childcare and backup workspace |
| `/cloud-backup/childcare/admin/login` | Redirect to unified login |
| `/cloud-backup/childcare/v1/...` | Childcare API |
| `/cloud-backup/childcare/media/...` | Childcare audio |

Cockpit's existing `/v1/` outside `/cloud-backup/` is unaffected. Do not bind a
second container to host 443 or add a public backend port. The identified
32070 entry point currently uses HTTP; production credential transport needs an
existing trusted network or an HTTPS entry point. No protected docforge,
1Panel, DNF or Mihomo containers need changes for this integration.

## Source and build

Use the user's separate Cloud Backup repository, `LezardZhang/cloud-backup-cockpit-addon`,
version 0.3.2, local source commit `311656ed23622edd9f240f14c651fd4004cb2361`.
Remote freshness was not verified because SSH authentication failed.
Do not copy only its Python modules: the image also requires migrations, admin
UI, protocol, Skill assets and its Linux x86_64/CPython 3.12 wheelhouse.
The unified Dockerfile uses the original pinned Python image and offline hashed
dependencies; it does not modify or vendor the Cloud Backup checkout.

Set these additional deployment variables in a private Compose environment:

```text
CHILDCARE_SOURCE_DIR=/absolute/path/ai-passport/backend
CLOUDBACKUP_SOURCE_DIR=/absolute/path/cloud-backup-source
CHILDCARE_ENV_FILE=/absolute/path/childcare.env
CHILDCARE_MEDIA_PATH=/absolute/path/childcare-media
```

The childcare env file follows `../.env.example`. Set its public base URL to the
actual external URL ending in `/cloud-backup/childcare`. Unified startup refuses
missing device/admin/Hermes/public tokens or an admin password. Preserve the
backup environment, especially `CLOUDBACKUP_KEY_ENCRYPTION_KEY`, admin token,
public URL and `CLOUDBACKUP_ROOT_PATH=/cloud-backup`. The original config bind
mount contains both independent databases; childcare media has a separate bind
mount writable by the existing container UID/GID (normally 10001:10001).
Do not mount a fresh directory over an existing database or media directory.

Apply `unified.override.yml` **last**, after all existing Cockpit and backup
Compose files, with all existing environment files. Review `docker compose
config --quiet` and build the candidate `cloud-backup` image first. The combined
image still listens on container port 8080. Only after backup and verification,
recreate `cloud-backup` with `up -d --no-deps cloud-backup`; preserve all other
services. The overlay defaults `PERSONAL_SERVICES_VERIFY_ONLY=1`: this disables backup
maintenance and rejects non-GET/HEAD/OPTIONS requests, including login, during
upgrade verification. Use admin Bearer tokens to verify Skill downloads. After
matching inventories, set it to `0` and recreate only this service to resume
normal operations. The usual `depends_on` relationship does not require recreating the
panel when its upstream service alias is unchanged.

The v2 upgrade on 2026-10-01 rebuilt only cloud-backup. Original database and object inventories passed comparison; other containers were unchanged. Private checkpoints and rollback scripts remain under /home/clouddata/releases/personal-services-20261001-v2. Legacy admin pages redirect to the unified workspace.

## Existing data and rollback

The inspected server binds `/home/clouddata/config` to `/config` and
`/home/clouddata/data` to `/data`. On 2026-10-01 it contained 3 users, 5 keys,
59 file rows and 521 version rows; SQLite quick_check was `ok`. These counts
are a point-in-time baseline, not a guarantee that writers have stayed idle.

1. Pause clients and scheduled writes/maintenance for the final comparison.
   Take an online consistent backup using the existing backup management API,
   download it and verify it using the original restore tool. Preserve the old
   image ID, Compose/environment files and encryption key privately. Do not
   copy a live SQLite database and its WAL as independent files.
2. Run the inventory tool on the paused original database and object directory:
   `python check_existing_data.py /home/clouddata/config/cloud-backup.db /home/clouddata/data > before.json`.
   It reads SQLite in a transaction, checks integrity and hashes every active
   object plus existing metadata without emitting record values or credentials.
3. Keep those same directories and key in the candidate configuration. Database
   IDs, file paths, key hashes/encrypted copies and version identities remain
   in place; no existing backup data is transformed. If childcare already has
   data, create its own SQLite online snapshot and preserve its media, then
   restore the snapshot into a new `childcare.db` path with writers stopped.
   Never overwrite an existing destination. A blank childcare database is only
   appropriate for a first deployment without childcare server data.
4. Verify `/readyz`, existing key authentication, current and historical
   downloads, old configured Skill export and new childcare workflows. Compare
   the inventory again with `--compare before.json` before resuming writers.
   Key last_used_at is excluded from metadata fingerprints so verification reads
   do not create false differences. Other maintenance timestamps can change metadata: investigate
   differences instead of forcing a match or deleting data.
5. Roll back by restoring the old image/config and recreating only `cloud-backup`
   against the same backup directories and encryption key. Keep childcare DB and
   media even while rolled back. Never use `down -v`, erase volumes or restore
   over active writers. Original Cloud Backup restore remains the recovery path
   for an independently verified backup if files were damaged.

Regression tests seed the original service, preserve an existing key and two
versions through unification, verify old/new Skill exports, test both prefixed
and proxy-stripped requests, reject corrupt objects and return to the original
service. These tests do not establish that production data has been migrated.

## Configured Skills

Cloud Backup's existing per-key Skill ZIP endpoints remain unchanged. Childcare
adds authenticated `GET /admin/api/skills/{hermes|public|device}` under its own
prefix and download buttons in its console. ZIPs include `SKILL.md`, a configured
`connection.json` and a dependency-free Python GET helper. Unzip the chosen
folder into the Agent's skills directory; the address and token are ready to use.
ZIP responses use `Cache-Control: no-store`. Treat the downloaded package as a
credential; do not publish it. Childcare roles currently share one token per
role, so rotation revokes all packages for that role, not just one download.
The helper refuses redirects so credentials stay at the configured endpoint.

## Fitness module boundary

The current fitness Android client may continue using the old file API until
its exact repository, data formats and ownership/prefixes are identified.
Do not infer ownership from a filename keyword or migrate all backup users into
one fitness account. A future `/fitness/` module should own a separate database
and schema version, with an importer that verifies source hashes, records source
version IDs and is idempotent. Retain original file versions after import.
APK endpoint/schema changes and a paired configured Skill require tests against
the confirmed client. This work is pending project identification.

## Validation

With Cloud Backup dependencies installed and its source on PYTHONPATH:

```sh
PYTHONPATH=/absolute/cloud-backup/src python -m pytest ../tests -q
PYTHONPATH=/absolute/cloud-backup/src python -m pytest /absolute/cloud-backup/tests -q
```

Run from this directory; use Python 3.12. The unified tests skip when Cloud
Backup is absent, so a skipped suite is not compatibility validation.

## Deployment from the installed image

`install_existing.py` targets only the identified Cockpit `cloud-backup` service.
It checks the installed source hash, extends its immutable image with
`Dockerfile.installed`, retains a verified consistent backup, then compares
metadata and every active object's hash during a read-only cutover. Normal
operations resume only after both services' Skill exports pass. A failed cutover
restores the original image. Other containers and host port mappings are untouched.

The release directory contains private `childcare.env`, deployment evidence,
`service-compose.sh` and `rollback.sh`. Use `sh service-compose.sh ps` to inspect
the service and `sh rollback.sh` to restore the original image. Keep this directory
and its configuration; future recreation must include `installed.override.json`.
The script refuses an existing childcare database and is not a general migration
tool or a rerunnable updater.


The v2 upgrade on 2026-10-01 rebuilt only cloud-backup. Original database and object inventories passed comparison; other containers were unchanged. Private checkpoints and rollback scripts remain under /home/clouddata/releases/personal-services-20261001-v2. Legacy admin pages redirect to the unified workspace.
