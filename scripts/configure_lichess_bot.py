"""Run with lichess-bot's Python after installing its requirements."""
import argparse
import pathlib
import yaml

parser = argparse.ArgumentParser()
parser.add_argument('lichess_bot', type=pathlib.Path)
parser.add_argument('--ponder', action='store_true', help='Enable thinking during the opponent turn')
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
config = args.lichess_bot / 'config.yml'
if config.exists():
    raise SystemExit('config.yml already exists; update its engine section using README.md')
data = yaml.safe_load((args.lichess_bot / 'config.yml.default').read_text())
data['token'] = 'REPLACE_WITH_YOUR_LICHESS_BOT_TOKEN'
data['engine'].update(dir=str(root), name='kittenfish', protocol='uci', ponder=args.ponder,
                      working_dir=str(root), uci_options={'Move Overhead': 100})
data['challenge']['variants'] = ['standard']
data['matchmaking']['allow_matchmaking'] = False
config.write_text(yaml.safe_dump(data, sort_keys=False))
config.chmod(0o600)
print('Created', config, '- set your bot token locally before starting lichess-bot.')
