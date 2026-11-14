import json
from pathlib import Path
R=Path(__file__).resolve().parent
b=json.loads((R/'boss-results.json').read_text());assert b['games']==16
large=json.loads((R/'large/report.json').read_text());control=json.loads((R/'control/report.json').read_text());local=json.loads((R/'match-summary.json').read_text())
lines=['# Results: larger root policy versus BOSS 3','',
'**BOSS 3 remains unsolved. This experiment does not establish a stronger bot.**',
'', 'Trained and retained a 24,833-parameter 128→128→64→1 policy (5.97× the shipped 4,161 parameters), plus a contemporaneous small-model control. Production and arena submission remain unchanged.',
'', '## Matched private BOSS 3 tests', '',
'Opponent verified as agent 4611894 in all 16 tests. Eight cases: four historical second-seat replays and four fresh first-seat seeds, identical per variant, alternating execution order. No model/weight tuning after seeing these outcomes.', '',
'| Policy | W/D/L | Score | First seat W/D/L | Second seat W/D/L |', '|---|---|---:|---|---|']
for name in ('baseline','candidate'):
    r=b['variants'][name];fmt=lambda x:f"{x['W']}/{x['D']}/{x['L']}"
    lines.append(f"| {name} | {fmt(r)} | {100*r['score']:.2f}% | {fmt(r['seats']['0'])} | {fmt(r['seats']['1'])} |")
lines+=['',f"Paired score difference: {100*b['paired_score_delta']:+.2f} percentage points; approximate 95% interval {100*b['approximate_95_ci'][0]:.2f} to {100*b['approximate_95_ci'][1]:.2f}. Eight cases are too few for a stable strength estimate. Seat samples use different maps; historical replay cases are not untouched holdout.",'',
'| Case | Baseline | Larger policy | Unit-margin change |','|---|---|---|---:|']
for r in b['paired_cases']:lines.append(f"| {r['case']} | {r['baseline']} | {r['candidate']} | {r['margin_delta']:+d} |")
lines+=['', 'No reported invalid actions, timeout errors or zero-candidate RNG observations in this batch. Internal elapsed maxima are recorded in boss-results.json; they are not judge CPU accounting or proof of timing safety.', '',
'## Supplementary local matches', '',
f"24 fresh canonical wall layouts, both seats, 49 ms: **{local['W']}W/{local['D']}D/{local['L']}L**, {local['score']:.2f}% against the shipped small-policy baseline; paired approximate 95% interval {local['paired_lo']:.2f}–{local['paired_hi']:.2f}%. Exact-RNG same-family local simulator; no judge timeout enforcement. No demonstrated improvement.", '',
'## Learning and implementation', '',
'| Model | Best epoch | Validation loss | Test top1 | Test top3 |','|---|---:|---:|---:|---:|']
for name,r in [('Retrained small control',control),('Larger policy',large)]:lines.append(f"| {name} | {r['best_epoch']} | {r['validation']['loss']:.5f} | {100*r['test']['top1']:.2f}% | {100*r['test']['top3']:.2f}% |")
lines+=['',
'The shipped Kaggle small checkpoint previously scored 48.94% test top1. Validation, not test or boss results, selected the larger checkpoint. Reused test maps have appeared in prior research. Larger model overfits after epoch 13; capacity alone did not improve imitation.', '',
'- Standalone C source: 99,119 bytes, below the historical 100,000-byte cap.',
'- FP16 storage, float32 inference; all decoded parameters match Python exactly.',
'- 4,444 complete historical test groups: zero top1 changes from FP16 storage; C versus packed NumPy max logit error 9.54e-6, zero top1 disagreements.',
'- Root scoring on 500 archived positions: median 0.148 ms, p95 0.303 ms, max 0.443 ms, including first-call decoding. Local timing, not judge certification.',
'- Existing full engine regression and ASan/UBSan suites pass. Disabled-policy depth-four results match baseline on 100 positions (value, action, nodes and completed depth).',
'- 12 persistent protocol turns pass legal-command checks; max round-trip 256.5 ms includes startup. No claim of a safe judge timing margin.',
'', '## Decision', '',
'Retain this candidate as a completed negative/inconclusive capacity experiment; do not replace the current bot or submit it as a demonstrated improvement. Boss score remains around break-even in this pilot, and local regression estimate is negative. Review all prior experiment families in REVIEW.md.', '',
'Next useful learning experiment should improve targets/data or learned evaluation influence, rather than merely adding width to root ordering. Any such change must still win matched BOSS 3 tests—especially second seat—and survive separate maps. Current task trained and tested the larger policy; reliable BOSS 3 superiority was not achieved.', '',
'Artifacts: large/ and control/ checkpoints, histories and portable weights; candidate.c; export.json; parity.json; disabled-parity.json; root-cost.json; data-audit.json; match-summary.json; boss-results.json; boss/ raw logs and independent Java reconstruction. Reproduction instructions in README.md.']
(R/'RESULTS.md').write_text('\n'.join(lines)+'\n')
