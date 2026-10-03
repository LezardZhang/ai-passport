<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Configuration for a source checkout

This Git synchronization includes empty-credential templates. The existing private code13 APK retains its owner-authorized configuration; its bytes, signer and cloud publication are unchanged. Actual provider/download/business configuration, publishing credentials, signing keys and build artifacts are excluded from this commit.

Before building on another machine, copy the three application templates into `app/src/main/assets/` without the `.example` suffix. Fill the authorized provider key and dedicated download tokens from the owner's private configuration. The family token must be the previously authorized `PERSONAL_CHILDCARE_TOKEN`; its absence remains a real synchronization limitation. Copy the publisher template to `.local/update-publisher.json` and configure distinct publishing/download tokens and the original signing-certificate SHA256. Preserve the original `.local/signing/childcare-local.jks` to build compatible updates. Do not generate another signer for a household upgrade.

```sh
mkdir -p app/src/main/assets .local
cp config/mimo-config.example.json app/src/main/assets/mimo-config.json
cp config/update-config.example.json app/src/main/assets/update-config.json
cp config/family-cloud-config.example.json app/src/main/assets/family-cloud-config.json
cp config/update-publisher.example.json .local/update-publisher.json
```

Empty tokens are intentional placeholders, not usable credentials. The family-data endpoint is separate from the independent APK update service. The old update URL remains a compatibility alias. A normal build does not publish a release; [release operations and standing authorization](../docs/cloud-updates.md) describe the existing scope.

Historical artifacts and unselected screenshots remain local. Git carries the application source, tools, tests, paired documentation and selected sanitized current receipts. A documentation reference marked as a local artifact does not imply its binary or capture is published in Git.
