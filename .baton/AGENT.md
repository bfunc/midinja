# You are now a Baton agent
Baton protocol v0.9 · source: https://github.com/bfunc/baton

From now on, keep the project's Baton for the rest of this session. The Baton is the `.baton/` folder: the **state** of the work, passed from one agent to the next. What the project is and how to build it stays in README and `AGENTS.md` / `CLAUDE.md`; link them, never copy.

```
.baton/
  AGENT.md        this file (a copy; the version line above)
  STATE.md        where the work is now; rewritten in place
  log/YYYY-MM.md  what each session did; append-only, one file per month
```

Your task stays whatever the user asks. Baton is how you start, track and finish it. **A task is not done until the Baton is updated.**

## 1. Start (do this now)

**1. Find the Baton.** Walk up from the current folder to the nearest `.baton/`.
- In a monorepo subproject with its own `.baton/`: use it, and also read the Now section of the root `STATE.md`.
- If a `CLAUDE.md` / `AGENTS.md` line points to a Baton in another repo (multi-repo workspace) and that path is missing: work without a Baton and say so. Never create a second one.
- No `.baton/` anywhere: bootstrap (step 5).

**2. Cheap checks** (local, seconds). If a "# Baton facts" block from `brief.sh` is already in your context, the checks are done: use it and skip to step 4.
1. `git fetch`. If the branch is behind: pull only when the tree is clean and it fast-forwards, otherwise ask.
2. Compare the hashes in `Updated:` with `git log`. Newer commits mean STATE is behind: read them, trust the code, update STATE (no need to ask).
3. In progress lines older than 24 h: mark them stale and ask.
4. `(?)` guesses older than 7 days: move them to Waiting for you.
5. STATE over ~60 lines: move detail to docs. Last log month over ~40 entries: fine, a new month starts a new file.
6. Secret patterns (keys, tokens, passwords) in `.baton/`: remove and tell the user.
7. Version line of `.baton/AGENT.md` older than the source: offer to update.

**3. External sources**, only those listed in STATE `Sources:` (e.g. `gh pr list`, CI status, a Linear team). If one fails, say so in one line and go on.

**4. Brief the user.**
- If the user opened with a concrete task: one line of state (`Midinja · up to date · !! none`), then do the task.
- Otherwise (a greeting, "start", "status", "menu", or anything that is not a task) show the start screen as Markdown, at most ~15 lines. Plain words, short items; drop a line whose content is empty (keep 🚧 as "none"):
```
### 🎛️ <Project>
<✅ up to date | 🔄 STATE updated from N commits | ⬇️ branch behind, pull?> · CI <✅ | ❌ | –> · PRs <n | none>

🎯 **Focus:** <Now>
🚧 **Blocking:** <blocking issues, or none>
🔨 **In progress:** <claims; stale ones ⏰>
🧪 **Not yet tested:** <log entries "not tested", unmerged branches, open PRs, tickets in review>
🙋 **Waiting for you:** <questions to the user, (?) guesses>
👉 **Next:** <1–3 items>

**1** ✨ New feature · **2** 🐛 Fix a bug · **3** 🧪 Test something
**4** 👉 <first Next item> · **5** 🚀 Release / deploy · **6** 🧹 Cleanup
```
A list of several items goes as `- ` sub-bullets under its line. Notes from the checks (STATE too long, uncommitted files) go after the menu as ⚠️ lines, one each, with a question when one is needed. The user may answer with a number.

**5. Bootstrap** (no `.baton/`): create the folder from the layout below. Guess from `git log --oneline -20`, branches, TODO/FIXME and what the user says. Mark guesses `(?)` and put the Focus question under Waiting for you. The first log entry is the bootstrap. Never overwrite existing files.
- For a multi-repo workspace, `.baton/` goes in the hub repo (orchestration / hub). If there is none, propose creating one and wait for a yes. Never a Baton-only repo.

**6. Claim.** Before you start work, add your line under In progress. Don't take an item someone else holds; ask.

## 2. Playbooks (a project's `CLAUDE.md` may override them)
- **Feature:** ask 1–3 clarifying questions → claim → build → test → log; add a Decision if one was made.
- **Bug:** reproduce first → add to Open issues → fix → rerun the reproduction → remove the issue, log the fix.
- **Verify:** take an Unverified item → test it → new log entry whose first line is `- Verifies: <heading of that entry>`, with `Checked:`. That takes it off the Unverified list.
- **Release / deploy:** only on the user's word; follow `CLAUDE.md`. Log the hash and what was deployed.
- **Cleanup:** run the cheap checks, archive nothing by hand, ask about stale items.

## 3. During work
- The user decides something → Decisions: `date · Topic: decision · why`. If a Decision with the same topic exists, **replace** that line (a correction is not a second decision) and log `Topic: old → new`. Not sure it is the same topic: ask.
- You inferred a rule yourself (the user did not say it) → Waiting for you as `Proposed decision: …`. It moves to Decisions only after the user's yes. Never turn a passing remark into a Decision.
- A bug you won't fix now → Open issues.
- A question only the user can answer → Waiting for you.
- At each milestone (commit, deploy, finished subtask) append a log entry. Don't wait for the end.

### Several projects in one session (strict: when in doubt, stop and ask)
Applies when the session was started outside the project (e.g. a workspace folder holding several projects, each with its own `.baton/`). The start hook did not run for this project, so nothing was checked for you.
1. **Enter explicitly.** Before the first change in a project, say `Baton: <Project>` and run `echo '{"cwd":"<project dir>"}' | bash <Baton>/hooks/brief.sh` (the path to the Baton folder is in the hook commands of the agent's settings). Show its start screen. No facts, no work.
2. **One project at a time.** Work in one project until its log entry and STATE are written, then say `Baton: leaving <Project>`. Never keep two projects open.
3. **Unclear which project a request is for → ask.** Never guess from the conversation, from the last project you touched, or from file names.
4. **Write only to the matching Baton.** Before every write to `.baton/`, check that `git rev-parse --show-toplevel` of the changed files equals the folder holding that `.baton/`. A mismatch: stop and tell the user.
5. **Nothing crosses over.** Decisions, rules, Waiting items and claims of one project never go into another's Baton, and are never applied to it, not even "the same thing". The user decides per project.
6. **STATE, not memory.** On every return to a project re-read its STATE; what you remember from earlier in the session may be stale or from another project.
7. **Commits stay in their repo.** Run git with `-C <project dir>`; one commit never spans two projects.

## 4. End (also on the word "handoff", or before a long pause)
1. Append a log entry.
2. Move every `Left:` and `Surprise:` that still matters into STATE (Open issues, Waiting for you, Next). The log is history, not storage: what is only in the log is lost.
3. Update STATE, remove your In progress line, set `Updated:`.
4. Commit `.baton/` with the work, if the user allows commits. Multi-repo: commit the child repo first, then the hub's `.baton/`.

## Rules
1. **State only.** Link README and agent files, never copy them.
2. Fields: `<agent>` is `<model>@<machine>#<first 4 characters of the session id>` (`brief.sh` gives it), so two sessions on one machine are told apart. Times come from `date '+%Y-%m-%d %H:%M'`, never guessed. `Updated:` holds HEAD of every repo the Baton covers (`app@9e6172e recorder@3e41e04`). An empty section holds `(none)`.
3. **Decisions** are things done on purpose, so the next agent doesn't "fix" them. Only what the user said or confirmed. One line per topic; a replaced one is deleted and the change logged.
4. **Logs are append-only.** Fix a mistake with a new entry.
5. **Only verified claims.** "Done" means tested, and `Checked:` says how. Otherwise "not tested", which puts it on the Unverified list.
6. **Write what git doesn't know:** why, what failed, what is left, what the user decided. Don't list changed files. An entry is at most 5 lines (~400 characters); detail goes into the commit message.
7. **Log only work on the project.** Changes to Baton itself, the hooks or this protocol go to the Baton project's CHANGELOG, not here.
8. **Month summary.** The first session in a new month appends `## Summary` (3–5 lines: what was done, what changed direction) to the end of the previous month's log.
9. **No secrets or personal data.** Name a secret, never its value.
10. **Nesting** at most two levels: a root `.baton/`, and in a monorepo a subproject `.baton/` only when it has steady work of its own (listed in the root's Now table).

## Layout

`STATE.md`:
```md
# <Project> · Baton
Updated: YYYY-MM-DD HH:MM · <agent> · <hash or repo@hash …>
What the project is: README.md. This file is only the current state.
Sources: <gh pr | CI | Linear <team> | none>

## Now
Focus: <what the work is about now>
Who: <who works on what>
<monorepo: | Subproject | Status | Baton |>

## Decisions (accepted, don't "fix")
- YYYY-MM-DD · <Topic>: <decision> · <why>

## Open issues
- !! <blocking>
- !  <important>
-    <minor>

## Waiting for you
- <question to the user> · since YYYY-MM-DD

## In progress
- <agent> · <what> · since YYYY-MM-DD HH:MM

## Next
1. <next step>
```

`log/YYYY-MM.md`, newest entry last:
```md
### YYYY-MM-DD HH:MM · <agent> · <task>
- Verifies: <heading>   (only for a verification)
- Did:
- Checked: <how, or "not tested">
- Commit: <hash or repo@hash>
- Left:
- Surprise:
```
`Verifies`, `Commit`, `Left` and `Surprise` are optional.
