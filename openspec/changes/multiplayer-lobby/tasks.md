# Tasks

- [x] 1.1 Provide a Windows host/join entry point. The startup Qt Quick menu creates, browses and joins persistent relay rooms; this is the accepted alpha hosting path.
- [x] 1.2 Show participants, reject overflow/incompatible clients and handle pre-start departures. Automated coverage exercises the bounded 2-8-player roster; the real two-player lobby passed alpha acceptance on 2026-10-29.
- [x] 1.3 Add a host-only Start action with a minimum of two humans; do not require ready flags. The Start Game UI performs the authoritative role draw, reveal and synchronized gameplay bootstrap; the real two-player flow passed alpha acceptance on 2026-10-29.
- [x] 1.4 Implement and test the 0.5:1 weighted Hunter draw using deterministic injected randomness and wire it to the accepted lobby start flow.
- [x] 1.5 Assign one human to slot 1, others to unique slots 2-8, and broadcast the same result. Assignment, validation, codec and authenticated relay integration are implemented and covered by the accepted lobby flow.
- [x] 1.6 Initialize only participating players and their starting Builders; remove online role switching.
- [x] 1.7 Preserve the separate solo no-AI hotkey mode (players 1/2); ongoing solo gameplay testing confirms the mode remains available.
- [x] 1.8 Validate the real two-player Windows host/join/start/reconnect flow. **User acceptance, 2026-10-29:** hosting and reconnect work well enough for alpha. Four/eight-player graphical testing and further polish move to alpha follow-up.
- [x] 1.9 Replace command-line pair provisioning with a persistent bounded relay room directory supporting public access and optional authorized player tokens.
