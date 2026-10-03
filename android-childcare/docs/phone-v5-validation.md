<p align="right"><a href="phone-v5-validation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Phone 0.5.0 validation - 2026-10-03

Samsung SM-S9280, Android16/API36, serial R5CX12FFTFF. Same-signer `adb install --no-incremental -r` upgraded code4 to code5 / 0.5.0-local. No data clearing, uninstallation or synthetic household writes occurred. Android accepted the replacement; a database byte comparison was not performed. The pulled installed APK SHA256 `ae592a33d6ed741461442ad7d245725492ccb4623ccc25b0ae6da25c9ad2ffed` equals the canonical verified delivery. Install (local artifact: `../reports/phone-v5-install.log`), version (local artifact: `../reports/phone-v5-version.txt`), launch (local artifact: `../reports/phone-v5-launch.txt`), UI/hash receipt (local artifact: `../reports/phone-v5-ui.json`).

PASS12 read-only assertions: Care speech entrance, vertical timeline and direct feeding/diaper entries, caregiver entrance, installed version, automatic checking, manual check button, data-preservation instructions, default enabled after blank legacy-channel migration, return to Care and installed APK hash. Private physical screenshots/XML remain under `.local/phone-v5/` and are excluded from the source archive.

During the bootstrap stage, the phone reached its bundled HTTPS channel and displayed the fixed status for a channel without a published release. A separate credential-free health request passed normal certificate verification and received HTTP404. This confirms the update endpoint remains undeployed, rather than a successful OTA. Public health check (local artifact: `../reports/update-public-health-v5.json`), actual deployment status (local artifact: `../reports/cloud-deployment-status-v5.json`).

At the time of this bootstrap: physical-phone OTA was NOT RUN. Production deployment was blocked by automatic browser review pending explicit access to the 1Panel origin. Actual download/system-installation/data-continuity acceptance passed on an isolated emulator using a local HTTPS service and synthetic data. No physical speech accuracy, maximum storage/RAM or production capacity test was run. Build: PASS. Host tests: PASS. Device tests: PASS for the stated physical read-only checks and local-emulator upgrade scope. Unverified: production publishing, physical OTA and runtime capacity. See [cloud updates](cloud-updates.md).

This document records the earlier bootstrap stage. The subsequently authorized deployment and live private-cloud delivery are recorded in [cloud acceptance](cloud-acceptance.md).
