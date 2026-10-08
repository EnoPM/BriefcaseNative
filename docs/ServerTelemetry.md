# Server telemetry SDK (work in progress)

This API is for **dedicated-server UE4SS C++ mods**. It is not part of the
client SDK and has no Discord dependency. A ranked mod can subscribe to
authoritative observations, queue them off the game thread, and send them to
its own service or bot. Never perform HTTP requests inside an event callback.

`briefcase::deceive::server::EventCollector` is instantiated by each mod and
called from that mod's `CppUserModBase::on_update()`. Its `subscribe()` method
returns a move-only token; destroying the token removes the callback. The
collector and its subscriptions must be destroyed before the mod DLL unloads.
Callbacks are synchronous and exceptions are contained at the SDK boundary.
The SDK is statically linked into each mod, so subscriptions are local to that
mod rather than a cross-mod event bus.

```cpp
#include <Briefcase/DeceiveInc/ServerEvents.hpp>

class RankedMod : public RC::CppUserModBase {
    briefcase::deceive::server::EventCollector events_;
    briefcase::deceive::server::EventCollector::Subscription subscription_ =
        events_.subscribe([this](const auto& event) {
            // Copy the event into a bounded, durable worker queue here.
        });

    void on_update() override { events_.update(); }
};
```

The collector currently emits **PhaseChanged** from the reflected
`DeceiveIncMatchGameState.GamePhase` property. It polls at most four times per
second while subscribed, assigns a new match ID when the game state changes or
returns to pregame,
and suppresses repeated observations of the same phase. `match_id` plus
`sequence` forms a per-mod event deduplication key. The initial observation
reports the current phase; it does not claim that the mod witnessed the start
of the match.

Other event kinds are contract reservations, **not implemented telemetry**:
`EventCollector::supports(kind)` reports this explicitly. A metadata dump from
the current dedicated-server build (2 October 2026) established the following
reflected candidates. It confirms their signatures, but does not yet prove
when the game invokes them or whether an XP event is suppressed in private
matches.

The observed `DeceiveIncServer-Win64-Shipping.exe` SHA-256 is
`F2125F09CBEB7922A4912706CC546477454CE229C15ED477AF2731A21C828FD3`.

| Desired ranked metric | Reflected server candidate and remaining validation |
| --- | --- |
| Eliminations | `DeceiveIncGameStateBase:HandleXPEvent(DIPlayerState, DIXPEvent, int)` with `DIXPEvent::Kill`; confirm that it represents a player elimination rather than NPC/assist or repeated award. |
| Terminal, retinal scan and vault entry | The same XP hook has `VaultComputer`, `ReticalScanner` (game spelling) and `EnterVault`. Compare each award with an actual completed interaction. |
| Printed keycard | `KeycardPrinterActor:SetPrintedKeycard(Actor)` is a candidate. The XP values `Keycard_Green/Blue/Purple/Gold` describe acquisition, not necessarily printing. |
| First podium pickup and later possession | XP values `FirstObjectivePickup` and `PickupObjective`; `DeceiveIncMatchGameState.ObjectiveCarrier` and `OnObjectivePicked` also exist. Confirm order and repeat behavior. |
| Damage dealt and taken | `HealthComponent.OnHealthChanged` carries `HealthDelta`, `InstigatedBy` and `RawDamage`. Prefer effective health loss for a score; confirm shields, healing, self/team damage and instigator mapping. |
| Survival to later phases | `DeceiveIncMatchGameState.GamePhase` plus `DIPlayerState.bIsDead`; snapshot the players present at each phase boundary. |
| Winner | `DIPlayerState.bWon`, `DeceiveIncMatchGameState.MatchResult`, `ExtractedPlayer`, `LastManStanding` and `OnMatchResultsPosted` exist. Confirm the final authoritative timing and team winners. |

For this build, the relevant `DIXPEvent` values are `Kill=2`,
`EnterVault=18`, `FirstObjectivePickup=19`, `PickupObjective=20`,
`Extract=21`, `VaultComputer=22`, and `ReticalScanner=38`. The enum also has
keycard acquisition values 9–12. Production code should resolve names from
`/Script/DeceiveInc.DIXPEvent` rather than bake these numbers into a mod.
`HandleXPEvent.EventAmount` is an XP-event amount; it must not be interpreted
as an elimination or terminal count without a gameplay check.

The dump also shows `DISerializedStats` with cumulative lifetime win and kill
totals; these must not be mistaken for per-match events. Before enabling each
metric, add a build-specific reflection contract check and validate invocation
and attribution during a real match. A ranked
identity must come from a verified server-side platform/account identifier;
display names and UObject addresses are not stable or trustworthy. The
optional `account_id` stays empty until such an identifier is validated.
`Engine.PlayerState.UniqueId` is a `UniqueNetIdRepl`; its safe conversion to a
stable EOS identity has not yet been established in this SDK.

## Live validation

A temporary, capped UE4SS Lua `RegisterHook` probe was tested on 2 October
2026. It observed `HealthComponent:HandleTakeAnyDamage` calls during a match.
Immediately after a vault terminal interaction completed, the server raised
an access violation with UE4SS on the crash stack. The probe recorded no XP or
vault-count callback before the failure, so the exact callback responsible is
not established. The probe was removed from the development server and must
not be shipped or re-enabled as-is. The earlier crash dump from a restart is a
separate shutdown failure and does not establish the vault crash cause.

Further tests should isolate one candidate at a time on a disposable server,
or prefer read-only state observation where it can preserve player attribution.
Do not claim an event is supported merely because its reflected signature is
present. Runtime validation also needs a stable server-side account identity.

The replacement development probe is `tools/ServerEventProbe`, a standalone
native executable that reads Unreal's `Saved/Logs` files from outside the
server process. It installs no hook, loads no DLL into the game and requires no
server restart. `--replay <log>` classifies an existing trace;
`--follow <Saved/Logs> --output <observations.jsonl>` watches new lines and
log rotation. It records phase changes, completed interactions, objective and
keycard resource changes, health-change logs, final match result and crashes,
with the original line as evidence. These are **candidates**:
the log uses temporary Unreal object names, omits the damage instigator and
does not expose a verified account ID. This probe cannot supply ranked scores
or replace an authoritative production event adapter.

In one completed match on 2 October 2026, replay found eight phase changes
(including the return to pregame), 173 completed interactions, 84 health-change
lines and one final result. The interaction lines included three distinct
`BP_VaultUnlockTerminal` completions, one `BP_KeycardPrinter` completion,
pickup of the printed keycard, an objective terminal and an extraction
terminal. The final summary reported `MissionSucess_ObjectiveExtracted` and
`WinningFID: 0`. The probe does not equate these temporary actor names or
faction numbers with a durable player identity. It also cannot yet establish
eliminations, damage attribution or whether each logged interaction deserves
ranked credit. The server's raw log contains authentication tokens in other
lines, so never distribute the full log as telemetry evidence.

At the objective podium, the same log recorded a `Mission_Objective` resource
addition to the interacting spy immediately before `EXTRACTION_ARRIVING`. This
is a stronger pickup clue than the interaction name alone. Resource removal
after the match may be ordinary cleanup rather than a transfer, so the probe
reports raw additions and removals without assigning ranked semantics.

Test a complete match while noting which player performs each action: kill a
player, finish a vault terminal and retinal scan, print and acquire a keycard,
enter the vault, pick up and transfer the package, deal and take damage, reach
later phases, then extract or win by last player standing. Compare the calls
and their order against the actual actions, including negative cases such as
NPC damage and repeated interactions. Confirm the final winner and a stable
account identity separately before enabling ranked scoring. Remove the probe
from the development server after this validation.

For delivery to Discord, the eventual ranked mod should persist events with
`match_id` and `sequence`, retry asynchronously, and reconcile final results
against server match statistics. This keeps network failures from blocking
the game thread or dropping the only copy of a result.
