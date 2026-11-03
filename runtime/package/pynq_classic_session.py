"""Use the preserved classic owner only after the native owner retired fully."""
import json
from pathlib import Path
import sys
import pynq_menu

if __name__=='__main__':
    interrupted=pynq_menu.launch_classic_game(int(sys.argv[1]))
    root=Path(__file__).resolve().parent
    final=json.loads((root/'classic_active.json').read_text())
    assert final['stage']=='returned' and final['hp_restored'] and final['program_freed']
    (root/'classic_result.json').write_text(json.dumps(dict(pass_=True,interrupted=interrupted,final=final),indent=2)+'\n')
