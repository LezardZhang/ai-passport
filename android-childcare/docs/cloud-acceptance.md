<p align="right"><a href="cloud-acceptance.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Live private cloud delivery acceptance — 2026-10-03

The owner authorized the new `/home/clouddata/android-childcare-updates/` directory, independent update service, related HTTPS proxy configuration backup and dedicated update route. Deployment used the authorized existing 1Panel session and direct scoped file navigation. No existing business database was read or modified. A broad `/home` listing was refused by automatic review; subsequent navigation went directly to the authorized directory. No alternate access bypass was used.

The private deployment archive SHA256 is `64b5ec864055c55b85b2e73c02e3caff900b200ddc91ca4ffe89312ddacc684c`. All8 payload files were verified before starting the Python3.12 container. The service listens only on `127.0.0.1:8916`, with a192MiB RAM limit and64-process limit. Its actual container health is healthy. A point sample after publication was32.77MiB/192MiB and2 processes; peak memory/load capacity was not measured.

The actual IP TLS virtual host is `/opt/1panel/www/conf.d/docforge.conf`, served by `1Panel-openresty-ahKZ`. Its existing included proxy directory now contains the additional `android-childcare-updates.conf`. The original vhost and cloud-backup route hashes remain unchanged. Both were backed up under `/home/clouddata/android-childcare-updates/proxy-backup-20261003T071444Z`. Nginx validation passed before and after adding the new route, followed by a graceful reload. [Scoped deployment receipt](../reports/update-cloud-deployment.json), deployment screenshot (local artifact: `../reports/update-cloud-deployment.jpg`).

## Current caregiver release, code13

Code13 / 0.6.6-cloud is published with the original signer. Caregiver registration now has only “I am the baby's ___”; new records use that title, while saved historical attribution stays unchanged. Both manifest aliases return code13, its APK URL uses `/android-updates/apks/`, and authenticated downloads through both aliases match the signed local APK. Android build and 134 host assertions passed; isolated Android caregiver persistence passed 16 assertions, and the normal registration form was visually verified with one input. Physical code13 installation is pending because the phone is not currently visible to ADB. The family-sync credential remains absent, so real household cloud synchronization is not claimed. [Code13 delivery](../reports/caregiver-title-delivery-v13.json), registration form (local artifact: `../reports/caregiver-title-form-v13.png`).

## Route migration and code12 acceptance

On October 3 the primary `/android-updates/` route became live. The coordinating deployment task reports that only the independent update container was recreated with `XIGUA_UPDATE_PUBLIC_BASE=https://162.14.108.234/android-updates`; Cloud Backup was not recreated. Local read-only verification confirms both health endpoints return 200, anonymous manifests return 401, authenticated manifests are identical, and both authenticated APK aliases match the signed code12 artifact.

The local private publisher now uses the new primary URL, retaining its credentials and expected signer. Code12 metadata keeps its original legacy APK URL; the new server base applies to subsequent releases. No code12 republish or version-only bump was needed. Samsung SM-S9280 already has code12 / 0.6.5-cloud installed with the original signer. During this verification the normal update page showed automatic checks enabled, “Latest 0.6.5-cloud” and a last successful check at 17:55 before any manual check. The normal UI does not expose the successful channel source, so the displayed result alone cannot distinguish a primary request from a fallback. [Migration verification](../reports/update-migration-v12.json).

## Historical code5/code6 release and phone checks

| Check | Result |
| --- | --- |
| Public HTTPS health, standard certificate verification | PASS, HTTP200 |
| Unauthenticated manifest and actual APK request | PASS, denied with401 |
| Download credential accessing owner history | PASS, denied with401 |
| Publisher history access | PASS, HTTP200 |
| Actual signed code5 publication/readback | PASS |
| Samsung normal app checking cloud code5 | PASS, recognized0.5.0-local |
| Formal code6 / 0.5.1-cloud publication/readback | PASS |
| Authenticated cloud APK bytes/hash | PASS, equal to signed local artifact |
| Samsung normal app discovery/download/staging verification | PASS |
| Samsung Android system installation | PENDING first-use source authorization |

The formal code6 APK SHA256 is `a8b5a54babc425badf5b97fc204ab9e9b85664d0e6af1133226c418701af1fa2`. Its DEX, resources and built-in assets match the accepted code5 runtime; only `AndroidManifest.xml` and signature material differ. The original signing certificate is retained. The synthetic code6 / `6-test-only` fixture was never published or installed on the physical phone. Runtime continuity (local artifact: `../reports/update-runtime-continuity-v6.json`), [cloud verification](../reports/update-cloud-verification-v6.json), phone download (local artifact: `../reports/phone-cloud-download-v6.json`).

Samsung SM-S9280 / Android16 API36 reached Android's first-use “Install unknown apps” permission for Xigua Childcare. The owner was asked to authorize only this app as an installer source. No permission change or system installation is claimed while that response is pending. Pre-upgrade timeline, family and provider UI fingerprints were saved without exporting their private contents; post-install comparison awaits installation. No physical data clearing, uninstallation, synthetic household writes or instrumentation occurred. Private phone UI captures remain under `.local/phone-cloud/`.

## Future updates

Increase the Android version code above the latest published release, retain the original signer, verify the feature change and run `./tools/release-update.sh --notes-file /path/to/notes.txt` from `android-childcare/`. It builds, validates, publishes and reads back the private cloud release. APK publication does not require rebuilding the server or changing proxy configuration. Opening the app checks at most once per6hours; a manual check is available in Settings → Version and Updates. Downloads and Android installation confirmation remain explicit. See [cloud operation](cloud-updates.md).

Historical code6 validation — Build: PASS, actual code6 APK and official SDK signature/package checks. Host tests: PASS,129 Android assertions; accepted identical runtime also has85 backend cases,354 Android regression assertions and45 update-specific Android assertions. The current repository complete gate passed after deployment; gate log (local artifact: `../reports/repository-gate-v6.log`). Device tests: PASS for the actual public HTTPS/publisher checks and physical-phone discovery/download/staging scope. Unverified: physical system installation/data-continuity comparison, server peak/load capacity and minimum-phone update memory/storage reserves.
