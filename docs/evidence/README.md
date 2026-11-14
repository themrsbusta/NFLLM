# Verification evidence

These files preserve selected records from the NF-DOS guest verification
included in the project handoff. Absolute Windows workspace paths were replaced
with `<LOCAL_WORKSPACE>`; measurement values and guest output were retained.

The test model files, disk images, compiled executables, and DOS/4GW runtime are
not included. The evidence records the configurations and hashes of the inputs
used at the time of testing.

The captured guest logs preserve the original program output, including one
Portuguese diagnostic string. The published source translates that message to
English; the 1.0 release executable comes from the supplied snapshot and may
still emit the original wording.

The 1.0 Experimental Research Release reports summarize additional NF-DOS and
QEMU proxy runs from the supplied research snapshot. Their raw `build/logs/`
files and private handoff records are not part of the public source archive;
those paths in the stage reports identify records in the original snapshot.
The release note states which checks were verified during publication and
which results rely on the supplied evidence.
