# Task: favour quests give the Friend rank (task 0285; for marriage, tasks 0270 and 0276)

Status: ordered by the coordinator on 2026-11-17 (toddhow: real favour quests, not a test aid, should give the relationship rank). Windows only.
Local commits on a worktree branch from `19d7dd9d38`, not pushed.
Goal: a marriage-eligible NPC (`PotentialMarriageFaction`) becomes the player's Friend (relationship rank 1) through the game's own favour dialogue and
Papyrus fragments, and then offers "Interested in me, are you?" with the Amulet of Mara worn, with no `marriage test friends`.

## 1. Survey (Skyrim.esm; scripts decompiled with houseCARL from `Skyrim - Misc.bsa`; the plugin read with a small Python reader, aggregates only)

Which scripts call `SetRelationshipRank` (a byte scan of every `qf_*`, `tif__*`, `dlc*` .pex of the five masters): about 140, among them every
`qf_favor*` quest fragment script and the TopicInfo fragments of the **FavorJobs** families. The favours come in two kinds:

**A. Dialogue jobs (no quest to start; the line is offered by anyone in the job's faction; the rank is raised in the line's own fragment):**

| family (quest) | the line | what the fragment does | marriage-eligible members |
| --- | --- | --- | --- |
| `FavorJobsBeggars` (`FavorJobsBeggarsGiveMoneyTopic`, INFOs 0x60022 and 0x92348) | give a beggar 1 gold (needs GetItemCount(Gold) >= 1) | `TIF__00060022`: RemoveItem gold 1, speaker AddItem, `FavorJobsBeggarsAbility.Cast`, a message, **rank 0 -> 1** | Angrenor Once-Honored (Windhelm, Candlehearth Hall) |
| `FavorJobsGatherWheat` (wheat, potato, leek, gourd, cabbage, nirnroot topics) | sell the crop to the foreman | quest script `SellWheat(foreman)`: **rank 0 -> 1**, pays gold | Avrusa Sarethi |
| `FavorJobsMineOre` (one INFO per ore keyword) | sell ore | `FavorJobsMineOreScript.SellOre(speaker, keyword)`: **rank 0 -> 1**, pays gold | Pavo Attius |
| `FavorJobsChopWood` | sell firewood | `SellWood(speaker)` | Temba Wide-Arm, Gilfre, Aeri |
| `FavorJobsDrunks` (`TIF__000B026C`) | give the drunk a drink | `Actor.ShowGiftMenu(alcohol list)` then **rank 0 -> 1** and a story event | Cosnach (Markarth Warrens) |

**B. Quest favours (a quest from the radiant story manager, accepted by the giver's dialogue, finished at a stage fragment):** `Favor001` (carry an
item to a target, `Fragment_2` sets rank 1), `Favor013` and `Favor017` (kill a target), `Favor018`, `Favor019`, `Favor031`, `Favor054`, `Favor104`
(a dungeon boss), `Favor109`, `Favor110`, `Favor151`, `Favor153`, `Favor157`, `Favor205`, the jarl and steward favours `Favor250` to `Favor258`
(rank 3, `FavorJarlsMakeFriends`). Their givers that are marriage-eligible: Revyn Sadri (019), Sondas Drenim and Aeri (001), Roggi (205), Shahvee
(204), Borgakh and Ghorbash (154), the scouts of Many Marshes, Omluag and Octieve San (013), Uthgerd, Benor and Cosnach (017), Ysolda (110).

Voice types: the seven marriage INFOs select the speaker by voice type (CTDA fn 426, a form list or one voice type). By the data Angrenor (0xEA266)
matches INFO 0x74DE1 (list), Cosnach (0x13AD4) INFO 0xCDA18 (direct), Pavo Attius INFO 0x74DE1, Avrusa Sarethi INFO 0xCDA19.

## 2. What was found by running it (Windows, five masters, `tools/skymarriage_check`, real clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`)

Run folder `I:\Projects\OpenEden-work\followers\favours` (`gen_fav.py` makes the input script and the PLAN, `run.sh` the settings; kept frames in
`keep/`). Settings: `marriage = true`, `quest alias packages = true`, `papyrus`, `journal`, 500 gold, **`marriage test friends` empty**.

- **The beggar favour needed no code at all.** Angrenor Once-Honored (Windhelm, Candlehearth Hall, `skyrimse:b82231`, in `PotentialMarriageFaction`, voice list
  of INFO 0x74DE1) offers "Here. Have a gold piece. (1 gold)" (the line of `FavorJobsBeggarsGiveMoneyTopic`, offered by the game's own conditions:
  `GetItemCount(Gold) >= 1`). The click runs `TIF__00060022.Fragment_0` after Goodbye (the box pauses the world): gold 500 to 499, `Spell.Cast`,
  `Message.Show`, `AddItem` to the beggar and **`SetRelationshipRank(player, 1)`** (natives counted: `actor.setrelationshiprank=1`, `spell.cast=1`,
  `message.show=1`, `objectreference.additem=1`, `objectreference.removeitem=1`).
- **Then the whole marriage chain with no test aid** (`MRG` and `PAPYRUS` lines): Angrenor's gift, a teleport to the Temple of Mara in Riften (test
  aid: the walk is not built), Maramal's three talks (stage 5 `TIF__00074DE2`, stage 10 `TIF__00074DE4`, the 200 gold amulet `TIF__00026B5C`, stage 15; gold
  299 and `amulet=1`), the Equip click (`INV equip 000c891b: ok`), a teleport back, and **Angrenor now offers "Interested in me, are you?"** (frame
  `keep/full1_6.png`: three buttons, the gold line, the marriage line and Goodbye); "Yes. Yes I am." answers "It's settled then. Brief as life can be in
  Skyrim, at least we'll have each other." (`keep/full1_7.png`), `TIF__00074DE3` runs and after Goodbye **stage 20 runs `QF_RelationshipMarriageCourti_00074793.Fragment_28`**;
  the journal lists **The Bonds of Matrimony: [ ] Speak to Maramal about arranging your wedding** (`keep/full1_8.png`).
- **Negative control** (`gen_fav.py ... nobeg`: the same run without the gold line): the amulet is worn (gold 300, `amulet=1`) and Angrenor offers only the
  gold line (`keep/ctl_5.png`, greeting of 1 topic): no marriage offer without the Friend rank.
- **Determinism.** Three runs of the whole scenario: the trace lines (`dialogue line`, `PAPYRUS quest 00074793`, `INV equip`, `MRG talk/cell`, `NTALK`)
  are byte-identical in all three; the `JOURNAL rebuilt` counter line differs by one in the first run (an ambient radiant quest), the other two are identical.
- **Wheat family.** Avrusa Sarethi (Sarethi Farm, `skyrimse:b105470`, marriage-eligible) offers "I have nirnroot to sell." only when the player holds nirnroot
  (a test item: `inventory test items = 59b86:5`); the line ran `TIF__000EDF02.Fragment_0` -> `FavorJobsGatherWheatScript.SellNirnroot`: rank set,
  `RemoveItem` and `AddItem` ran, but `Form.GetGoldValue` is a tolerant native here, so the reward was 0 gold. The ore family (`SellOre`) has the same gap in
  `GlobalVariable.GetValueInt`.

## 3. Stage log: the sale rewards (done 2026-11-17)

- **Tests first** (`apps/components_tests/skyrimse/testskyrimsefavourvalues.cpp`, 4 tests, registered): run against the tolerant natives, **3 of 4 failed**
  (`Form.GetGoldValue` read the hook, `GlobalVariable.GetValueInt` truncated toward zero including a negative value, a generated sale paid
  count times value); the 4th (an item the hook does not know is worth 0) passes by nature. After the change 4 of 4 pass.
- **Code:** `PapyrusWorldHooks::goldValue(FormId)` (default -1, a trailing additive virtual), `PackWorldEnv::goldValue` filled in
  `skyrimse_game_pack.cpp` from `mInventory->table().item(id)->mValue`, `PackWorldHooks::goldValue` forwards; natives `Form.GetGoldValue`
  (a negative answer is 0) and `GlobalVariable.GetValueInt` in `papyrus_host.cpp` (same lookup order as `GetValue`).
- **In game** (`CELLID=SarethiFarm`, `TARGET=skyrimse:b105470`, `inventory test items = 59b86:5`, 500 gold, real clicks): "I have nirnroot to sell."
  runs `TIF__000EDF02.Fragment_0`; after Goodbye `actor.setrelationshiprank` and `form.getgoldvalue 00059B86` are logged and **gold goes 500 to 550**
  (nirnroot's base value is 10, 5 sold). Three runs: the dialogue, native and gold lines are identical (only the ambient `PAPYRUS natives` summary
  lines differ in an ambient radiant quest's counters, as in the earlier runs). A first attempt clicked before the conversation was ready (input
  frames must be a few hundred after the `talk` step).
- **Ore sale not shown:** Pavo Attius (`skyrimse:b78769`, `KolskeggrMinePavosHouse`) offered only the two small-talk topics with 5 `OreIron`
  (0x71CF3) in the inventory; the ore INFOs' conditions (a keyword item count) were not looked into. `GetValueInt` is covered by the unit test only.
- **Morrowind unchanged:** `tools/worldobjects_check` 110 objects OK against `integ2`'s build; fixed Balmora frame `1c5d47ad0b5dc32d` for both.

## 4. Missing favour types, ranked

1. **Ore sale** (Pavo Attius and the other miners): find why the line is not offered (the INFO conditions on the ore keywords), then the same two natives apply.
2. **Wood sale** (`FavorJobsChopWood`: Temba Wide-Arm, Gilfre, Aeri): the same pay rule; needs firewood in the inventory.
3. **Drunk gift** (`FavorJobsDrunks`, Cosnach): `Actor.ShowGiftMenu(list)` is not a native here; needs the gift window to hand back the chosen item.
4. **Quest favours** (`Favor001`, `013`, `017`, `019`, ...): the radiant story manager starts them; carrying an item or a kill is each its own path.
5. **Kill favours** and **jarl favours** (`Favor250` to `258`, rank 3): need the quest chains.

## 5. Decisions for toddhow

- Gold value of an item the pack does not know is 0 (not an error): a sale of such an item pays nothing.
- No existing test was edited. No setting was added.

## 6. Not run

The Linux gate and GCC `-Werror`, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review, mutation checks, the ore, wood and
drunk favours, the quest favours.
