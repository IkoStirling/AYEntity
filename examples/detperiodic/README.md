# Authored periodic hazard and treatment

Stage49 uses actual collision Enter/Stay/Exit and logical processing intervals.
The hero starts at HP6 in a Hurt2 zone, period3; selected hand attempts1/4/7 reach
HP4/2/0, then accepted death routing relocates outside and restores6. A patient
starts HP1 in a Heal2 zone, period2;1/3/5 saturate3/5/6, later attempts are Unchanged.
Manual tick10 accepts equalHP recovery without a numerical Progress write.

Build `AYEntity_DetPeriodicExample`. The module owns Host/Scene/ticks; source is
loaded outside Sim. `--interactive` supports idle/right/left, respawn, hurt/heal,
status and fresh Live save/load. Source/input use exact software values; native
printing is only presentation. No wall timer, independent poll or second pose owner.
Necessary local Release core, authored Level6, tool and actual six-path Host acceptance passed; native final VM remains deferred.

The final hand record is heroHP6/Hurt3/death1/restores2/revival1/restoreWrite1; patientHP6/Heal3. Hazard3 attempts end inactive after actual Exit, while treatment6 attempts leave nextEligible13. Live/Record/Replay/Load/Seek/stdin agree in full checkpoint, per-tick and restored suffix bytes; saved boundary4 has a pending interval.
