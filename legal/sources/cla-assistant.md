# Hosted CLA Assistant

## Record

- Relationship: Service Integration
- Status: Current planned hosted signing infrastructure; external activation is still required
- Confidence: Confirmed design; deployment state must be verified on GitHub
- Upstream: [Hosted CLA Assistant](https://cla-assistant.io) and [CLA Assistant Source](https://github.com/cla-assistant/cla-assistant)
- Revision: Hosted service revision is controlled externally; the current source package identifies itself as version 1.8.3
- Governing Terms: The self-hostable source is Apache License 2.0 with bundled third-party terms; hosted-service terms, privacy practices, permissions, and availability are separate

## UVSR Relationship

UVSR plans to configure the hosted GitHub App to present the project CLA and
report `license/cla` for pull-request committers. No CLA Assistant source code
is vendored or run by the repository. The app records acceptance; the UVSR
agreement itself, not the bot, defines the contributor's rights grant.

## Evidence

- [Hosted Setup And Security Procedure](../documentation/CLA-ASSISTANT.md)
- [Signing Metadata Schema](../documentation/cla-assistant-metadata.json)
- [Contributor Agreement](../licenses/UVSR-CONTRIBUTOR-LICENSE-AGREEMENT.md)
- [Contributor Privacy Notice](../documentation/CONTRIBUTOR-AGREEMENT-PRIVACY-NOTICE.md)

## Commercial Clearance

Before relying on the flow, activate the app, pin the exact public signing copy,
require the correct app-produced status, test multi-author behavior, preserve
signature exports, and review provider terms and privacy handling. An installed
bot alone does not prove a valid agreement or expand third-party rights.
