# Platform development

Read README.md and backend/API documents relevant to the change.
In the full product workspace read ../docs/platform-development.md, ../docs/configuration.md and ../ai/README.md.
Consume Kernel through explicit Q2_KERNEL_DIR. Standalone builds must not require parent source/config files.
Product builds inject Q2_GENERATED_DIR; settings belong to root q2.json, not a copied platform config.
Keep SDKs here, marshal ISR/threads to the owner, and use q2_result_t with documented errors.
Follow four-space/110-column style and explain state transitions. Test host behavior and affected builds.
Never advertise providers that are not implemented or confuse PC simulation with hardware validation.
