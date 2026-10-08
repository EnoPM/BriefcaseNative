# Windows server installation video storyboard

This is a recording plan for a beginner-facing video about installing Briefcase and its server mods. It does not cover installing the dedicated game server or using Server Manager.

## Recording setup

- Record only the display of a clean Windows VM. Keep the VM disk and all working files on `D:`; do not record the host desktop.
- Prepare an up-to-date Deceive Inc. dedicated server inside the VM before recording. Use a disposable path such as `D:\DemoServer\DeceiveInc\Binaries\Win64`.
- Use a fresh browser profile and a fictional Windows account name. Hide browser history, notifications, personal bookmarks, IP addresses, passwords and administration files.
- Use the current Windows server release and the current Windows UE4SS releases of Player Cap, Pregame Timer and Suspicion Control. Do not use older `Briefcase/Mods` packages.
- Record at 1920×1080. Keep the mouse visible, zoom File Explorer enough to read paths, and add English captions. Pause briefly after each completed action.

## Sequence

| Time | Show on screen | Caption / narration |
| --- | --- | --- |
| 0:00–0:15 | Open the prepared server's `DeceiveInc\Binaries\Win64` folder and point to `DeceiveIncServer-Win64-Shipping.exe`. | “Your dedicated server is already installed. This is the Win64 folder where Briefcase and the mods go. Stop the server before changing files.” |
| 0:15–0:40 | Open the [BriefcaseNative latest release](https://github.com/EnoPM/BriefcaseNative/releases/latest). Download the asset whose name starts `BriefcaseNative-Server-windows-x64-` and ends `.zip`. | “Choose the Windows **Server** archive. Do not download the client archive or source code.” |
| 0:40–1:10 | Open the ZIP, select its contents, and extract them directly into the prepared `Win64` folder. Show `version.dll`, `Briefcase`, and `ue4ss` beside the Shipping executable. | “Put the archive contents directly in Win64. Do not create a second Win64 folder.” |
| 1:10–1:50 | Visit the latest releases for [Player Cap](https://github.com/EnoPM/Briefcase.PlayerCap/releases/latest), [Pregame Timer](https://github.com/EnoPM/Briefcase.PregameTimer/releases/latest), and [Suspicion Control](https://github.com/EnoPM/Briefcase.StaminaControl/releases/latest). Download each `*-windows-x64-*.zip` asset and extract each archive directly into the same `Win64` folder. | “Each mod is a separate download. All three archives go into the same Win64 folder.” |
| 1:50–2:20 | Open `Win64\ue4ss\Mods`. Show the three mod folders. Open `mods.txt` and add the three lines below, then save. | “UE4SS loads only the mods enabled in this file.” |
| 2:20–2:45 | Show the optional configuration files for Pregame Timer and Suspicion Control. Do not change their values in the first-install demo. Point out that Player Cap uses the server's `MaxPlayers` setting and does not need a mod setting. | “You can change mod settings later. For now, leave the defaults and confirm that the installation works.” |
| 2:45–3:15 | Start the server headless from Briefcase App; wait for startup and show that the `Briefcase`/UE4SS directories and logs exist. Show only non-sensitive log lines indicating the mods loaded. | “Briefcase App starts the server with the mods enabled.” |
| 3:15–3:30 | Return to File Explorer and recap the expected folders and enabled lines. | “If a mod is missing, first check that its archive was extracted into Win64 and its line is present in `mods.txt`.” |

`ue4ss\Mods\mods.txt` should contain these enabled entries once each:

```text
briefcaseplayercap : 1
briefcasepregametimer : 1
briefcasesuspicioncontrol : 1
```

## Review before sharing

- Recheck the release asset names against the live GitHub releases before recording.
- Confirm the recorded VM has no personal account, credential, public IP address or real administration password in view.
- Confirm the final video shows the exact Win64 destination, the three mod folders, the saved `mods.txt`, and evidence that the server started.
- Review the exported video at normal playback speed and verify that paths and captions are legible on a phone.
