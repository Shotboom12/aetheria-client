# Changes

## kyrian-venthyr-20261102-1

Kyrian robe rework and Venthyr robe removal. Client assets only; the server payload is unchanged (paired version bump).

- Kyrian male and female (`kyrianmale/female.m2` + `00.skin`): rebuilt robe skirt. Regularized texture coordinates (straight artwork lines, level rows), level hem, covered shins hidden (feet and ankles stay visible), chain skinning (pelvis, thighs, calves with same-side lean) so the cloth follows the legs in run and sit, back and hem widened to enclose both feet, and a reshaped Run animation (knee lift to about 63 degrees, shank tucked back, foot under the hip). The idle form is preserved.
- Venthyr male and female (`vampire_male00.skin`, `vampire_female00.skin`): robes removed. The robe skirt geoset (1302) is collapsed and the covered-leg geoset (20300) is restored to its original body skin group, so a robed Venthyr always shows legs (and hands) instead of a skirt. Skin files only; the Venthyr `.m2` files are unchanged.
- Status: the Kyrian result was accepted by the owner in game (2026-11-02). The Venthyr change was installed locally and reviewed from a screenshot; no server changes. Full gameplay acceptance of the races remains pending.
- Known: the Venthyr leg pattern resolution is limited by the Venthyr atlas layout (a narrow atlas strip, about 2x anisotropic); a kicked-back Kyrian foot can emerge below the hem in a high kick.

## kyrian-venthyr-20261027-1

Initial paired race-only overlay: current catalogs/palettes/models, Kyrian racial/charge/Hover code/data, Venthyr racials with legal-area Bat travel and ground Hover, and the latest robe/2H/Hover/Crimson Feast corrections. Visual/gameplay acceptance for the latest corrections is pending. Future selected updates will modify these same package paths, manifests and version together; no full game/source mirror is intended.
