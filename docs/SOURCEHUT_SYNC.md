# SourceHut Branch Synchronization

`.github/workflows/sourcehut-sync.yml` mirrors the complete GitHub branch set
to SourceHut. It runs automatically for every branch push, also runs when a
branch is deleted so pruning happens immediately, and can be started manually
from **Actions > Sync branches to SourceHut > Run workflow**.

The job uses `git push --force --prune`: every GitHub branch is force-updated
on SourceHut, and a SourceHut branch that no longer exists on GitHub is deleted.
Tags are intentionally not synchronized.

## Required Actions secrets

Add these repository secrets under **Settings > Secrets and variables > Actions**:

| Secret | Value |
|---|---|
| `SOURCEHUT_SSH_PRIVATE_KEY` | The complete private SSH key authorized on the SourceHut account that owns the repository. Keep the `BEGIN`/`END` lines. |
| `SOURCEHUT_REPOSITORY` | The SourceHut SSH URL, for example `git@git.sr.ht:~your-user/LeonOS-4`. |

Create a dedicated deploy key when possible, for example:

```sh
ssh-keygen -t ed25519 -f leonos4-sourcehut-sync -C leonos4-github-actions
```

Add `leonos4-sourcehut-sync.pub` to the SourceHut account, then put the
contents of `leonos4-sourcehut-sync` in `SOURCEHUT_SSH_PRIVATE_KEY`.

The workflow pins the current `git.sr.ht` SSH host keys and enables strict host
key checking. If SourceHut rotates its host keys, update the pinned entries in
the workflow before the next synchronization.

The workflow does not print either secret. The GitHub checkout token is used
only to fetch the repository's branches; the SourceHut remote is authenticated
with the separate SSH key.
