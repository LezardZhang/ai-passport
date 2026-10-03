<p align="right"><a href="cloud-updates.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Private cloud updates, Android 0.6.6

Code12 / 0.6.5-cloud is the published transition release. Its primary manifest is `https://162.14.108.234/android-updates/latest.json`; `/cloud-backup/android-childcare-updates/latest.json` remains a compatibility fallback. Both routes reach the independent update container, whose `XIGUA_UPDATE_PUBLIC_BASE` is now `https://162.14.108.234/android-updates`. Existing default legacy settings migrate to the primary URL; explicitly disabled or custom channels remain unchanged. The original Android package, signer and SQLite schema 3 remain.

Settings → Version and Updates shows the installed version, check status, last successful check, release notes, download and installation confirmation. Opening the app checks at most once per six hours by default; automatic checks can be disabled. Care shows a notice when a newer version is found. Checks do not download or install automatically. Android requires its installation confirmation and may request permission to install from this app.

Downloads use HTTPS without redirects and send the dedicated download credential only to the manifest's origin. The app checks SHA256, actual package/version/minimum SDK and the original installed signer before staging. Installation rechecks the staged hash and signer. Channel changes invalidate old results. Failed or cancelled downloads remove partial files; successful installation clears the consumed candidate. Records, people, settings and pending synchronization operations remain in the app's database.

## Current release

Code13 / 0.6.6-cloud simplifies caregiver registration to one title field. It is published through the new primary entry; both manifest aliases return the same release, and its APK URL uses `/android-updates/apks/`. Both authenticated APK aliases match the signed local artifact. This feature change required a new version code; code12 was not republished. [Code13 verification](../reports/caregiver-title-delivery-v13.json).

## Standing owner authorization

On October 3, 2026, the backend coordination task relayed the owner's explicit standing authorization for this existing private childcare APK and the update service at `162.14.108.234`. Its source task is `01a0fd0e-7c7c-7761-b5a2-27fc30907d1f`. Within the previously authorized scope, routine builds, signature verification, publication, local/server publishing-credential configuration, scoped token reads and embedding in the private APK, and related private-project Git synchronization do not need another owner confirmation. [Authorization record](../reports/owner-authorization-20261003.json).

The scope remains the childcare update directory `/home/clouddata/android-childcare-updates/`, the related HTTPS proxy configuration and routes, and the previously authorized read of `PERSONAL_CHILDCARE_TOKEN` from `cockpit-cloud-cloud-backup-1` with the baby's profile and care data through existing interfaces. Publishing credentials remain in the publisher/server configuration, outside the APK. Only authorized application/download/business tokens may be embedded; credential values must stay out of logs, reports and delivery summaries. This grant does not extend to administrator keys, unrelated private files, adult profiles, other applications or business-database changes. Git synchronization covers related changes in the owner's private project, not public distribution of credentials or unrelated work. Runtime tool approvals and the current coordination restriction on Chrome/1Panel still apply.

This record changes no artifact or acceptance result. Code13 remains published; no extra version is required for the authorization or sync audit. Physical code13 installation remains unverified while ADB cannot see the phone, and real family synchronization still requires the missing dedicated family credential.

## Release operation

Keep the original `.local/signing/childcare-local.jks` and private `.local/update-publisher.json`. The latter contains `public_base`, `publish_token`, `download_token` and `certificate_sha256`; its `public_base` is now `https://162.14.108.234/android-updates`. Credentials and the expected signer are unchanged. Only the scoped download token is included in the owner-authorized application asset; publishing credentials never enter the APK, logs or source archive. APKs contain the owner's authorized AI configuration and require authenticated private downloads.

After increasing `versionCode` and `versionName` in the Android manifest and validating the feature change, run from `android-childcare/`:

```sh
./tools/release-update.sh --notes-file /path/to/release-notes.txt
```

This runs host checks, builds with the existing signer, verifies the actual APK using SDK `apksigner`/`aapt`, uploads it and reads back the authenticated latest manifest. Normal builds do not publish. `python3 tools/publish-update.py prepare` verifies without contacting the server; `check` reads the cloud version. The publisher rejects wrong signers/packages, synthetic test APKs and changed releases whose version code does not increase. Keep encoded metadata below 7.6 KiB; large release notes receive a specific rejection. Standard HTTPS certificate verification stays enabled.

The independent service verifies the uploaded size/hash and bounded APK archive. The publisher and phone verify the actual cryptographic signature; the server itself does not run Android SDK signature verification. It accepts only the authorized package and configured signer declaration. A separate publishing credential is therefore required.

## Isolated deployment

The deployment bundle contains only `backend/app/android_updates.py`, its pinned runtime requirements, Dockerfile/Compose definition and the proposed Nginx location fragment. It does not rebuild the unified backend or migrate business data. Deployed server root: `/home/clouddata/android-childcare-updates/`. The new service binds only `127.0.0.1:8916` and the existing HTTPS proxy routes the specific update prefix to it. Preflight confirmed a free loopback port and the actual TLS virtual host. Back up the affected proxy file, validate Nginx before reload and retain the previous configuration for rollback.

Release publication streams one APK, verifies it, creates an immutable version/hash filename and atomically promotes `latest.json` only after that file is durable. Failed uploads preserve the old release. Identical publications can be retried; older APKs remain available. No automatic pruning or app database downgrading is provided. Rollback of a bad app release requires a repaired APK with a higher version code.

Configured server limits: one worker/upload, 120-second upload deadline, 64 MiB APK, 8 KiB encoded metadata header, 4,096 ZIP entries/2 MiB central directory, 128 MiB disk reserve, 2 GiB release quota and 1,000 immutable files. A crashed promotion can reuse an already verified artifact. Owner history returns at most 100 entries. Container configuration limits RAM to 192 MiB and processes to 64; these are configuration bounds, not measured production capacity. See [resource budget](resource-budget.md).

## Historical bootstrap validation

Local tests: 129 Android host assertions, 85 backend cases, 354 Android regression assertions and 45 update-specific Android assertions. The actual signed code-5 APK was published and read back through a local HTTPS service using a dedicated test CA and synthetic credentials. Synthetic code6 / 6-test-only is restricted to the local installer acceptance and cannot be published by the official release tool. Host (local artifact: `../reports/host-tests-v5.log`), backend (local artifact: `../reports/backend-suite-v5.log`), Android regression (local artifact: `../reports/android-tests-v5.log`), update checks (local artifact: `../reports/android-cloud-update-v5.log`), HTTPS publisher (local artifact: `../reports/update-local-publish-v5.log`), repository complete gate (local artifact: `../reports/repository-gate-v5.log`).

An isolated emulator downloaded synthetic code 6 through actual HTTPS, opened the normal app's update page, confirmed Android's system Update dialog and completed installation. Verification retained the care record and caregiver, baby settings, provider configuration hash and pending operation ID; installed-candidate cleanup also passed. The test CA is installed only in the instrumentation transport; the production app's TLS behavior remains standard. The final code-5 APK matches every non-signature entry of the accepted runtime. Installer continuity (local artifact: `../reports/update-install-continuity-v5.log`), normal app installer UI (local artifact: `../reports/update-installer-ui-v5.log`), runtime continuity (local artifact: `../reports/update-runtime-continuity-v5.json`), scoped private deployment bundle receipt (local artifact: `../reports/update-deployment-bundle-v5.json`).

The owner granted scoped deployment and access to the 1Panel origin. The independent service is **live** on the authorized server. The existing TLS virtual host includes `/opt/1panel/www/sites/docforge/proxy/android-childcare-updates.conf`; the original vhost and cloud-backup route hashes are unchanged. Their backup is in `/home/clouddata/android-childcare-updates/proxy-backup-20261003T071444Z`. Nginx validation/reload and normal public HTTPS verification passed. Unauthenticated manifests/APKs and download-token access to owner history return401. Real code5 and formal code6 releases were published and read back; authenticated APK bytes match the signed local artifact. [Deployment receipt](../reports/update-cloud-deployment.json), [public health](../reports/update-public-health-cloud.json), [release verification](../reports/update-cloud-verification-v6.json).

At the code5/code6 bootstrap stage, Samsung recognized the live release, found0.5.1-cloud, downloaded it and passed the production app's APK validation; system installation was then awaiting first-use installer authorization. That historical result does not describe the current code12 installation. One production container sample was32.77MiB against the configured192MiB limit; this is not a peak/load measurement. [Cloud acceptance](cloud-acceptance.md), runtime continuity (local artifact: `../reports/update-runtime-continuity-v6.json`).


## Transition endpoint

The update client is independent from Cloud Backup. The new primary route is live, and the server and private publisher use `/android-updates/`. The legacy `/cloud-backup/android-childcare-updates/` route remains available for older installed clients and as the transition client's fallback. Code12 is already available through both aliases. Both authenticated manifests are identical. Immutable code12 metadata retains its original legacy APK URL; APK downloads through either alias match the signed local artifact. Subsequent releases use the new primary APK path without republishing code12. Retire the legacy alias only after supported older clients have received the same-signer transition APK. [Current migration verification](../reports/update-migration-v12.json).

The endpoint synchronization changed only local publisher configuration and documentation. It did not republish code12 or rebuild production. The separate caregiver feature subsequently produced code13; Cloud Backup data was not altered.
