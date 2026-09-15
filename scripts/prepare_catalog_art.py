"""Prepare explicit art bindings from research and the committed module design catalogs.

Generation is performed with the built-in image tool; this script does not call an API.
"""
import argparse
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / 'assets/visual/catalog'
SUBJECTS = {
 'foundations': 'a precision physics experiment bench with optical lenses, laser interferometer, calibrated metal instruments and vacuum chamber',
 'energy': 'a toroidal fusion reactor with copper magnetic coils, steel pressure housing and restrained blue plasma',
 'propulsion': 'a spacecraft propulsion engine, large machined bell nozzle, turbopump plumbing and heat-resistant ceramic liner',
 'space_industry': 'an orbital construction yard with a partially assembled metallic spacecraft inside a skeletal gantry, distant planet',
 'life_medicine': 'a spacecraft medical laboratory with a sealed diagnostic bed, surgical instrument arms and sterile life support equipment, no patient',
 'planetary': 'a sealed planetary settlement with metal pressure habitats, airlocks and atmospheric monitoring towers on a rocky plain',
 'materials': 'a close product study of layered titanium, ceramic and carbon composite material samples under a precision testing probe',
 'computing': 'a powerful computing core with intricate silicon chips, copper heat pipes and neatly routed optical data cables',
 'sensors_comms': 'a precision deep-space sensor array with a large parabolic dish, optical telescope lenses and antenna booms on a metal mount',
 'military': 'a heavy spacecraft defense turret with paired electromagnetic rail barrels, armored base and visible cooling mechanisms',
 'logistics': 'a space freight transfer port with modular metal cargo containers, cargo tugs and articulated loading cranes',
 'social_admin': 'an empty futuristic civic council chamber, elegant circular seating surrounding a physical city model, realistic architecture',
 'xenoscience': 'an alien manufactured artifact with intricate unfamiliar machined metal geometry inside a sealed examination chamber, scientific instruments',
 'biotechnology': 'a precision biotechnology apparatus with glass bioreactor vessels, nutrient plumbing and a robotic micropipette examining cellular cultures',
 'synthetic_systems': 'a sophisticated autonomous maintenance robot with articulated metal manipulators inspecting a rugged computing rack',
 'biosphere_agriculture': 'a spacecraft hydroponic greenhouse with vivid green crops in metal planting trays, irrigation pipes and warm grow lights',
 'economic_trade': 'a busy futuristic orbital trade terminal with organized freight pallets, autonomous loaders and a distant transport ship through an observation window',
 'cybernetics': 'a highly engineered prosthetic hand and neural interface on a medical workbench, titanium joints, fine actuators and optical leads, no person',
 'research_infrastructure': 'a modern automated scientific laboratory with robotic experiment stations, microscopes, sample trays and precision instruments',
 'stellar_engineering': 'immense realistic metal solar collector platforms in orbit around a warm star, panel trusses and construction craft convey scale',
 'alternative_biochemistry': 'a cryogenic chemistry laboratory with sealed amber liquid sample vessels, frosted metal plumbing and low-temperature analysis instruments',
}
STYLE = ('Original serious hard-science-fiction Stellar Continuum game art. Photorealistic industrial visualization, '
 'real machined metal construction, bolts, seams, believable materials. Single clear subject in three-quarter view, '
 'centered with a little breathing room, readable at thumbnail size. Dark navy background with neutral key light '
 'and warm rim lighting; colorful material accents. Square composition. No text, letters, numbers, labels, logos, '
 'watermarks, UI, borders, people, cartoon styling or plastic toy surfaces.')

PROMPT_OVERRIDES = {"research-energy":"Use case: stylized-concept\nAsset type: square in-game research technology thumbnail for Stellar Continuum, original serious hard-science-fiction strategy game.\nSubject: advanced fusion reactor, a compact toroidal magnetic confinement chamber with machined steel housings, copper coils, ceramic insulators and a restrained blue-white plasma glow, photographed in a dark spacecraft engineering bay.\nStyle: photorealistic high-end industrial product visualization, convincing real metal construction, bolts, seams and cooling pipes; crisp silhouette readable as a small thumbnail.\nComposition: single primary apparatus centered, three-quarter view, fills 80% of square, dark navy backdrop, soft neutral key light and subtle warm rim light. No people.\nConstraints: no words, no letters, no numbers, no logo, no watermark, no borders, no cartoon, no plastic toy styling, no magical effects."}

def write(path, data):
 path.parent.mkdir(parents=True, exist_ok=True)
 path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')

def main():
 parser = argparse.ArgumentParser()
 parser.add_argument('--content-repository', type=pathlib.Path, required=True)
 parser.add_argument('--content-ref', default='3aeeb9a4')
 args = parser.parse_args()
 def committed(path):
  return json.loads(subprocess.check_output(['git', '-C', str(args.content_repository), 'show', f'{args.content_ref}:{path}']))
 station = committed('docs/content/HUMAN_STATION_DESIGN.json')
 fleet = committed('assets/models/alien-fleet-v2/fleet-manifest.json')
 jobs, research, modules = [], [], []
 index = json.loads((ROOT / 'data/research/v1/index.json').read_text())
 for domain, file in index['domain_files'].items():
  art = 'research-' + domain
  jobs.append(dict(id=art, prompt='Use case: stylized-concept. Asset type: research family illustration. Subject: ' + SUBJECTS[domain] + '. ' + STYLE))
  for node in json.loads((ROOT / 'data/research/v1' / file).read_text())['nodes']:
   research.append(dict(id=node['id'], name=node['name'], domain=domain, depth=node['graph_depth'], art=art))
 for module in station['modules'] + station['weaponModules']:
  for stage in module['levels']:
   id = module['id'] + '--l' + str(stage['level'])
   subject = f"Human space station {module['name'].lower()} module, upgrade level {stage['level']}: {stage['appearance']}. Show the detachable assembly with its hardpoint connector plate and mounting lugs; this is one module, not a complete station."
   if module['id'] == 'human_station_heavy_battery':
    armament = {1: 'one massive long electromagnetic railgun barrel', 2: 'two massive long parallel electromagnetic railgun barrels and broad side radiators', 3: 'three massive long parallel electromagnetic railgun barrels, heavy layered armor and large cooling fins'}[stage['level']]
    subject += f' Heavy battery means a capital-ship artillery gun emplacement: {armament}, elevated on a broad rotating armored turret with a reinforced recoil cradle. It must clearly read as a heavy gun turret, not an electrical storage battery, cargo pod or engine. No firing or explosions.'
   jobs.append(dict(id=id, prompt='Use case: stylized-concept. Asset type: equipment catalog concept illustration. Subject: ' + subject + ' ' + STYLE))
   modules.append(dict(id=id, family=module['id'], name=module['name'], race='terran_baseline', level=stage['level'], art=id,
    status='Concept artwork; station outfitting is planned', description=stage['appearance'], research=stage['requiredResearch']))
 for race in fleet['races']:
  for module in fleet['modules']:
   id = race['id'] + '--' + module['id']
   file = next(f for f in fleet['files'] if f['path'] == 'Modules/' + id + '.glb')
   modules.append(dict(id=id, family=module['id'], name=module['name'], race=race['id'], level=1, art=id,
    status='Model render; fleet asset candidate', description=module['description'], model='assets/models/alien-fleet-v2/' + file['path'], modelSha256=file['sha256'], research=[]))
 # A module unlock uses the exact same illustration as the module itself, when that
 # planned research ID eventually becomes visible. No planned nodes enter gameplay.
 planned = [dict(id=id, name=m['name'], domain='space_industry', depth=m['level'], art=m['art']) for m in modules for id in m['research']]
 write(DEST / 'catalog.json', dict(schemaVersion=1, research=research, plannedResearch=planned, modules=modules))
 for job in jobs:
  job['prompt'] = PROMPT_OVERRIDES.get(job['id'], job['prompt'])
 write(DEST / 'production/jobs.json', dict(generator='built-in image_gen', jobs=jobs))
 write(DEST / 'production/source.json', dict(contentCommit=args.content_ref, stationSource='docs/content/HUMAN_STATION_DESIGN.json', fleetSource='assets/models/alien-fleet-v2/fleet-manifest.json', note='Committed content only. No balance or research unlocks are added by this presentation catalog.'))
 print(f'{len(research)} research entries, {len(modules)} modules, {len(jobs)} generation jobs')

if __name__ == '__main__': main()
