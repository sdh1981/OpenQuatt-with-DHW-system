"""Genereert docs/dashboard/openquatt_ha_dashboard_duo_nl_v2.yaml.

Draai vanuit de repo-root:  python scripts/build_dashboard_v2.py

Waarom gegenereerd en niet met de hand onderhouden: het script leest eerst uit
de firmware-YAML welke entiteiten er werkelijk worden aangemaakt, en valideert
daarna elke verwijzing in de dashboarddefinitie daartegen. Een tikfout in een
entity-id wordt hier een harde fout in plaats van een leeg vakje in Home
Assistant. Bij de eerste run ving dat er acht.

V1 (openquatt_ha_dashboard_duo_nl.yaml) blijft met de hand onderhouden; dat is
het dagelijkse dashboard en wordt hier niet aangeraakt.
"""
import glob
import io
import os
import re
import sys

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'docs', 'dashboard', 'openquatt_ha_dashboard_duo_nl_v2.yaml')


class _Loader(yaml.SafeLoader):
    pass


_Loader.add_multi_constructor('', lambda loader, suffix, node: None)

_DOMAINS = {'sensor': 'sensor', 'binary_sensor': 'binary_sensor', 'text_sensor': 'sensor',
            'switch': 'switch', 'number': 'number', 'select': 'select',
            'button': 'button', 'climate': 'climate'}


def _slug(name):
    return re.sub(r'[^a-z0-9]+', '_', name.lower()).strip('_')


def firmware_entities():
    """Entity-id's die de firmware aanmaakt, afgeleid uit de package-YAML."""
    found = set()
    for path in sorted(glob.glob(os.path.join(ROOT, 'openquatt', '*.yaml'))):
        try:
            doc = yaml.load(io.open(path, encoding='utf-8'), Loader=_Loader)
        except Exception:
            continue
        if not isinstance(doc, dict):
            continue
        for section, domain in _DOMAINS.items():
            for item in (doc.get(section) or []):
                if not isinstance(item, dict):
                    continue
                name = item.get('name')
                if not name or not isinstance(name, str) or item.get('internal') is True:
                    continue
                # ${prefix} wordt per warmtepomp ingevuld.
                variants = ([name.replace('${prefix}', 'HP1 - '),
                             name.replace('${prefix}', 'HP2 - ')]
                            if '${prefix}' in name else [name])
                for variant in variants:
                    found.add(domain + '.openquatt_' + _slug(variant))
    # wifi_info levert zijn entiteiten via een genest blok; de parser hierboven
    # kijkt alleen op itemniveau en mist ze daardoor. Ze bestaan wel.
    found.add('sensor.openquatt_ip_address')
    return found


ENTS = firmware_entities()

# --------------------------------------------------------------------------
# Bouwstenen
# --------------------------------------------------------------------------


def head(icon, text):
    return {'type': 'heading', 'icon': icon, 'heading': text, 'heading_style': 'title'}


def sub(text):
    return {'type': 'section', 'label': text}


def ent(eid, name=None):
    d = {'entity': eid}
    if name:
        d['name'] = name
    return d


def rows(*items):
    """entities-kaart; strings worden secties, tuples worden entiteiten."""
    out = []
    for it in items:
        if isinstance(it, str):
            out.append(sub(it))
        elif isinstance(it, tuple):
            out.append(ent(it[0], it[1] if len(it) > 1 else None))
        else:
            out.append(it)
    return {'type': 'entities', 'show_header_toggle': False, 'entities': out}


def tile(eid, name=None, features=None, vertical=False):
    d = {'type': 'tile', 'entity': eid, 'vertical': vertical}
    if name:
        d['name'] = name
    if features:
        d['features'] = features
    return d


def when(card, eid, state='on'):
    """Toon deze kaart alleen als een conditie geldt."""
    card = dict(card)
    card['visibility'] = [{'condition': 'state', 'entity': eid, 'state': state}]
    return card


def when_not(card, eid, state):
    card = dict(card)
    card['visibility'] = [{'condition': 'state', 'entity': eid, 'state_not': state}]
    return card


def graph(hours, *pairs, title=None):
    """Verloop over tijd. Zonder grafieken is een warmtepompdashboard kaal:
    de getallen zeggen wat het NU is, de lijn zegt waar het heen gaat."""
    card = {'type': 'history-graph', 'hours_to_show': hours,
            'entities': [ent(e, n) for e, n in pairs]}
    if title:
        card['title'] = title
    return card


def gauge(eid, name, vmin, vmax, severity=None):
    card = {'type': 'gauge', 'entity': eid, 'name': name,
            'min': vmin, 'max': vmax, 'needle': True}
    if severity:
        card['segments'] = severity
    return card


def note(text, *conditions):
    """Markdown-kaart, eventueel met voorwaarden."""
    card = {'type': 'markdown', 'content': text}
    if conditions:
        card['visibility'] = list(conditions)
    return card


def is_on(eid):
    return {'condition': 'state', 'entity': eid, 'state': 'on'}


def is_off(eid):
    return {'condition': 'state', 'entity': eid, 'state': 'off'}


def is_state(eid, state):
    return {'condition': 'state', 'entity': eid, 'state': state}


def badge(eid, name=None):
    d = {'type': 'entity', 'entity': eid}
    if name:
        d['name'] = name
    return d


def grid(*cards):
    return {'type': 'grid', 'cards': [c for c in cards if c is not None]}


def view(title, path, icon, *sections, badges=None):
    v = {'title': title, 'path': path, 'icon': icon, 'type': 'sections',
         'max_columns': 3}
    if badges:
        v['badges'] = badges
    v['sections'] = list(sections)
    return v


# --------------------------------------------------------------------------
# VIEW 1 — NU
# --------------------------------------------------------------------------
ALLES_RUSTIG = [
    is_state('sensor.openquatt_dhw_fault', 'NONE'),
    is_off('binary_sensor.openquatt_cycling_waarschuwing'),
    is_off('binary_sensor.openquatt_instellingen_gewijzigd'),
    is_off('binary_sensor.openquatt_pressure_soft_warning_active'),
    is_off('binary_sensor.openquatt_supply_temp_soft_cap_active'),
    is_off('binary_sensor.openquatt_discharge_soft_cap_active'),
    is_off('binary_sensor.openquatt_lowflow_fault_active'),
]

V_NU = view(
    'Nu', 'nu', 'mdi:gauge',
    grid(
        head('mdi:alert-decagram', 'Aandacht'),
        # Alleen kaarten waarvan de conditie geldt. Is er niets, dan neemt de
        # rustmelding hieronder de plek in -- een lege kolom leest namelijk als
        # kapot, niet als "alles goed".
        note('### Geen bijzonderheden\n\n'
             'Geen storing, geen actieve begrenzing, geen cycling-incident, '
             'instellingen ongewijzigd.', *ALLES_RUSTIG),
        when_not(rows(('sensor.openquatt_dhw_fault', 'DHW-storing'),
                      ('button.openquatt_dhw_clear_fault', 'Fout wissen')),
                 'sensor.openquatt_dhw_fault', 'NONE'),
        when(rows(('sensor.openquatt_cycling_status', 'Cycling'),
                  ('sensor.openquatt_hp1_starts_2u', 'HP1 starts 2u'),
                  ('sensor.openquatt_hp2_starts_2u', 'HP2 starts 2u'),
                  ('button.openquatt_cycling_waarschuwing_bevestigen', 'Bevestigen')),
             'binary_sensor.openquatt_cycling_waarschuwing'),
        when(rows(('sensor.openquatt_instellingen_afwijking', 'Afgeweken'),
                  ('button.openquatt_instellingen_herstellen', 'Herstellen')),
             'binary_sensor.openquatt_instellingen_gewijzigd'),
        when(rows(('sensor.openquatt_pressure_hp1_level_cap', 'Druk cap HP1'),
                  ('sensor.openquatt_pressure_hp2_level_cap', 'Druk cap HP2')),
             'binary_sensor.openquatt_pressure_soft_warning_active'),
        when(rows(('sensor.openquatt_supply_temp_hp1_level_cap', 'Aanvoer cap HP1'),
                  ('sensor.openquatt_supply_temp_hp2_level_cap', 'Aanvoer cap HP2')),
             'binary_sensor.openquatt_supply_temp_soft_cap_active'),
        when(rows(('sensor.openquatt_discharge_hp1_level_cap', 'Persgas cap HP1'),
                  ('sensor.openquatt_discharge_hp2_level_cap', 'Persgas cap HP2')),
             'binary_sensor.openquatt_discharge_soft_cap_active'),
        when(rows(('binary_sensor.openquatt_lowflow_fault_active', 'Lage flow')),
             'binary_sensor.openquatt_lowflow_fault_active'),
    ),
    grid(
        head('mdi:home-thermometer', 'Systeem'),
        tile('sensor.openquatt_control_mode_label', 'Systeemmodus'),
        tile('select.openquatt_heating_control_mode', 'Verwarmingsstrategie'),
        rows(('sensor.openquatt_room_temperature_selected', 'Kamer'),
             ('sensor.openquatt_room_setpoint_selected', 'Gewenst'),
             ('sensor.openquatt_water_supply_temp_selected', 'Aanvoer'),
             ('sensor.openquatt_outside_temperature_selected', 'Buiten')),
        graph(24,
              ('sensor.openquatt_room_temperature_selected', 'Kamer'),
              ('sensor.openquatt_room_setpoint_selected', 'Gewenst'),
              ('sensor.openquatt_water_supply_temp_selected', 'Aanvoer'),
              ('sensor.openquatt_outside_temperature_selected', 'Buiten'),
              title='Etmaal'),
    ),
    grid(
        head('mdi:heat-pump', 'Warmtepompen'),
        # working_mode_label in plaats van het ruwe registergetal; "0" zegt niets.
        rows('HP1',
             ('sensor.openquatt_hp1_working_mode_label', 'Modus'),
             ('sensor.openquatt_hp1_compressor_frequency', 'Frequentie'),
             ('sensor.openquatt_hp1_water_out_temperature', 'Water uit'),
             ('sensor.openquatt_hp1_power_input', 'Opgenomen'),
             ('sensor.openquatt_hp1_cop', 'COP'),
             'HP2',
             ('sensor.openquatt_hp2_working_mode_label', 'Modus'),
             ('sensor.openquatt_hp2_compressor_frequency', 'Frequentie'),
             ('sensor.openquatt_hp2_water_out_temperature', 'Water uit'),
             ('sensor.openquatt_hp2_power_input', 'Opgenomen'),
             ('sensor.openquatt_hp2_cop', 'COP')),
        graph(12,
              ('sensor.openquatt_hp1_compressor_frequency', 'HP1'),
              ('sensor.openquatt_hp2_compressor_frequency', 'HP2'),
              title='Compressorfrequentie'),
    ),
    grid(
        head('mdi:flash', 'Vermogen nu'),
        gauge('sensor.openquatt_total_power_input', 'Elektrisch', 0, 4000,
              severity=[{'from': 0, 'color': 'green'},
                        {'from': 2500, 'color': 'yellow'},
                        {'from': 3400, 'color': 'red'}]),
        rows(('sensor.openquatt_total_heat_power', 'Warmte'),
             ('sensor.openquatt_flow_average_selected', 'Flow')),
        graph(12,
              ('sensor.openquatt_total_power_input', 'Elektrisch'),
              ('sensor.openquatt_total_heat_power', 'Warmte'),
              title='Vermogen'),
    ),
    badges=[
        badge('sensor.openquatt_control_mode_label', 'Modus'),
        badge('sensor.openquatt_room_temperature_selected', 'Kamer'),
        badge('sensor.openquatt_water_supply_temp_selected', 'Aanvoer'),
        badge('sensor.openquatt_outside_temperature_selected', 'Buiten'),
        badge('sensor.openquatt_total_power_input', 'Vermogen'),
        badge('sensor.openquatt_dhw_tank_top', 'Tank'),
    ],
)

# --------------------------------------------------------------------------
# VIEW 2 — WARM WATER
# --------------------------------------------------------------------------
V_DHW = view(
    'Warm water', 'warm-water', 'mdi:water-boiler',
    grid(
        head('mdi:water-thermometer', 'Nu'),
        tile('sensor.openquatt_dhw_state', 'Toestand'),
        rows(('sensor.openquatt_dhw_tank_top', 'Tank boven'),
             ('sensor.openquatt_dhw_tank_bottom', 'Tank onder'),
             ('sensor.openquatt_dhw_estimated_time_to_ready', 'Klaar over'),
             ('binary_sensor.openquatt_dhw_hp_request_active', 'HP levert'),
             ('binary_sensor.openquatt_dhw_element_active', 'Element aan')),
        gauge('sensor.openquatt_dhw_tank_top', 'Tank boven', 10, 70,
              severity=[{'from': 10, 'color': 'red'},
                        {'from': 42, 'color': 'yellow'},
                        {'from': 48, 'color': 'green'}]),
        graph(24,
              ('sensor.openquatt_dhw_tank_top', 'Boven'),
              ('sensor.openquatt_dhw_tank_bottom', 'Onder'),
              title='Tankverloop'),
    ),
    grid(
        head('mdi:rocket-launch', 'Snelboost'),
        tile('switch.openquatt_dhw_boost_now', 'Snelboost nu', features=[{'type': 'toggle'}]),
        # Reden en grenzen alleen tonen zolang er echt een boost loopt.
        when_not(rows(('sensor.openquatt_dhw_boost_reden', 'Reden'),
                      ('number.openquatt_dhw_boost_now_target', 'Doel tanktop'),
                      ('number.openquatt_dhw_boost_now_hp_stop', 'HP eruit bij')),
                 'sensor.openquatt_dhw_boost_reden', 'Geen boost actief'),
    ),
    grid(
        head('mdi:stairs-up', 'Tweede HP'),
        rows(('sensor.openquatt_dhw_single_hp_lead', 'Actieve unit'),
             ('sensor.openquatt_dhw_second_hp_assist_status', 'Assist'),
             ('sensor.openquatt_dhw_hp_thermisch_vermogen', 'Thermisch vermogen')),
    ),
    grid(
        head('mdi:gauge', 'Rendement'),
        rows('Laatste cyclus',
             ('sensor.openquatt_dhw_cyclus_cop', 'COP'),
             ('sensor.openquatt_dhw_cyclus_energie_in', 'Elektrisch erin'),
             ('sensor.openquatt_dhw_cyclus_energie_uit', 'Warmte eruit'),
             'Langere termijn',
             ('sensor.openquatt_dhw_cop_24h', 'COP 24 uur'),
             ('sensor.openquatt_dhw_cop_lifetime', 'COP totaal'),
             ('sensor.openquatt_dhw_cycles_24h', 'Cycli 24 uur'),
             ('sensor.openquatt_dhw_gemiddelde_kwh_per_cyclus', 'Gemiddeld per cyclus')),
        graph(168,
              ('sensor.openquatt_dhw_cyclus_cop', 'Cyclus-COP'),
              title='Rendement per cyclus, week'),
    ),
    grid(
        head('mdi:shower-head', 'Tappingen'),
        rows(('binary_sensor.openquatt_dhw_tapping_actief', 'Er wordt getapt'),
             ('sensor.openquatt_dhw_tappingen_vandaag', 'Vandaag'),
             ('sensor.openquatt_dhw_laatste_tapping_energie', 'Laatste tapping'),
             ('sensor.openquatt_dhw_tank_standby_loss', 'Standby-verlies')),
    ),
    grid(
        head('mdi:bacteria', 'Legionella'),
        rows(('sensor.openquatt_dhw_legionella_volgende_run', 'Volgende run'),
             ('sensor.openquatt_dhw_legionella_laatste_run', 'Laatste run'),
             ('sensor.openquatt_dhw_legionella_deferral_status', 'Uitstel'),
             ('sensor.openquatt_dhw_legionella_eta', 'ETA'),
             ('sensor.openquatt_dhw_legionella_elapsed', 'Verstreken')),
    ),
    badges=[
        badge('sensor.openquatt_dhw_state', 'Toestand'),
        badge('sensor.openquatt_dhw_tank_top', 'Boven'),
        badge('sensor.openquatt_dhw_tank_bottom', 'Onder'),
        badge('sensor.openquatt_dhw_cyclus_cop', 'Cyclus-COP'),
        badge('sensor.openquatt_dhw_tappingen_vandaag', 'Tappingen'),
    ],
)

# --------------------------------------------------------------------------
# VIEW 3 — VERWARMEN
# --------------------------------------------------------------------------
V_HEAT = view(
    'Verwarmen', 'verwarmen', 'mdi:radiator',
    grid(
        head('mdi:strategy', 'Strategie'),
        tile('select.openquatt_heating_control_mode', 'Strategie'),
        rows(('sensor.openquatt_control_mode_label', 'Systeemmodus'),
             ('sensor.openquatt_water_supply_temp_selected', 'Aanvoer nu'),
             ('sensor.openquatt_room_temperature_selected', 'Kamer'),
             ('sensor.openquatt_room_setpoint_selected', 'Gewenst')),
        graph(48,
              ('sensor.openquatt_water_supply_temp_selected', 'Aanvoer'),
              ('sensor.openquatt_outside_temperature_selected', 'Buiten'),
              title='Aanvoer tegen buitentemperatuur'),
    ),
    grid(
        head('mdi:chart-bell-curve', 'Adaptive Heating'),
        rows(('sensor.openquatt_adaptive_supply_offset', 'Geleerde offset'),
             ('sensor.openquatt_adaptive_heating_status', 'Status'),
             ('button.openquatt_adaptive_reset_learned_offset', 'Offset wissen')),
        graph(168,
              ('sensor.openquatt_adaptive_supply_offset', 'Geleerde offset'),
              title='Wat het model heeft geleerd'),
    ),
    grid(
        head('mdi:heat-pump-outline', 'Aanvraag per HP'),
        rows('HP1',
             ('sensor.openquatt_hp1_compressor_level', 'Gevraagd niveau'),
             ('sensor.openquatt_hp1_compressor_frequency', 'Frequentie'),
             ('sensor.openquatt_hp1_heat_power', 'Warmte'),
             ('binary_sensor.openquatt_hp1_defrost', 'Ontdooien'),
             'HP2',
             ('sensor.openquatt_hp2_compressor_level', 'Gevraagd niveau'),
             ('sensor.openquatt_hp2_compressor_frequency', 'Frequentie'),
             ('sensor.openquatt_hp2_heat_power', 'Warmte'),
             ('binary_sensor.openquatt_hp2_defrost', 'Ontdooien')),
    ),
    grid(
        head('mdi:pump', 'Flow'),
        rows(('sensor.openquatt_flow_average_selected', 'Gemiddeld'),
             ('sensor.openquatt_hp1_flow', 'HP1'),
             ('sensor.openquatt_hp2_flow', 'HP2'),
             ('select.openquatt_flow_control_mode', 'Flowregeling')),
    ),
    badges=[
        badge('select.openquatt_heating_control_mode', 'Strategie'),
        badge('sensor.openquatt_water_supply_temp_selected', 'Aanvoer'),
        badge('sensor.openquatt_adaptive_supply_offset', 'Offset'),
        badge('sensor.openquatt_flow_average_selected', 'Flow'),
    ],
)

# --------------------------------------------------------------------------
# VIEW 4 — KOELEN
# --------------------------------------------------------------------------
V_COOL = view(
    'Koelen', 'koelen', 'mdi:snowflake',
    grid(
        head('mdi:snowflake-thermometer', 'Nu'),
        tile('switch.openquatt_manual_cooling_enable', 'Koeling toestaan', features=[{'type': 'toggle'}]),
        rows(('binary_sensor.openquatt_cooling_enable_selected', 'Koeling vrijgegeven'),
             ('sensor.openquatt_cooling_supply_target', 'Aanvoerdoel'),
             ('sensor.openquatt_water_supply_temp_selected', 'Aanvoer nu'),
             ('sensor.openquatt_cooling_supply_error', 'Afwijking'),
             ('sensor.openquatt_cooling_demand_raw', 'Vraag'),
             ('sensor.openquatt_cooling_stop_reason', 'Stopreden')),
    ),
    grid(
        head('mdi:water-percent', 'Dauwpuntbeveiliging'),
        # De reden dat koelen begrensd wordt zit hier, niet bij de vraag.
        rows(('sensor.openquatt_cooling_dew_point_selected', 'Dauwpunt'),
             ('sensor.openquatt_cooling_dew_point_trend', 'Trend'),
             ('sensor.openquatt_cooling_minimum_safe_supply_temp', 'Minimale veilige aanvoer'),
             ('sensor.openquatt_cooling_dynamic_safety_margin', 'Dynamische marge'),
             ('sensor.openquatt_cooling_valid_room_count', 'Geldige ruimtes'),
             ('sensor.openquatt_cooling_room_count_selected', 'Ruimtes in gebruik')),
        graph(24,
              ('sensor.openquatt_cooling_dew_point_selected', 'Dauwpunt'),
              ('sensor.openquatt_cooling_minimum_safe_supply_temp', 'Ondergrens aanvoer'),
              ('sensor.openquatt_water_supply_temp_selected', 'Aanvoer'),
              title='Hoe dicht zit de aanvoer op het dauwpunt'),
    ),
    grid(
        head('mdi:heat-pump-outline', 'Per HP'),
        rows(('sensor.openquatt_hp1_cooling_power', 'HP1 koelvermogen'),
             ('sensor.openquatt_hp2_cooling_power', 'HP2 koelvermogen'),
             ('sensor.openquatt_cooling_assist_level_2nd', 'Tweede unit')),
    ),
)

# --------------------------------------------------------------------------
# VIEW 5 — ENERGIE
# --------------------------------------------------------------------------
V_ENERGY = view(
    'Energie', 'energie', 'mdi:lightning-bolt',
    grid(
        head('mdi:flash', 'Vandaag'),
        rows(('sensor.openquatt_electrical_energy_daily', 'Elektrisch'),
             ('sensor.openquatt_heating_electrical_energy_daily', 'Waarvan verwarmen'),
             ('sensor.openquatt_cooling_electrical_energy_daily', 'Waarvan koelen'),
             ('sensor.openquatt_heatpump_thermal_energy_daily', 'Warmte geleverd'),
             ('sensor.openquatt_heatpump_cop_daily', 'COP'),
             ('sensor.openquatt_heatpump_eer_daily', 'EER')),
    ),
    grid(
        head('mdi:counter', 'Sinds de laatste reset'),
        rows(('sensor.openquatt_electrical_energy_cumulative', 'Elektrisch'),
             ('sensor.openquatt_heatpump_thermal_energy_cumulative', 'Warmte'),
             ('sensor.openquatt_heatpump_cooling_energy_cumulative', 'Koeling'),
             ('sensor.openquatt_boiler_thermal_energy_cumulative', 'Boiler'),
             ('sensor.openquatt_heatpump_cop_cumulative', 'COP'),
             ('sensor.openquatt_heatpump_eer_cumulative', 'EER'),
             ('button.openquatt_reset_cumulative_energy_counters', 'Tellers wissen')),
    ),
    grid(
        head('mdi:chart-line', 'Rendement over tijd'),
        rows(('sensor.openquatt_scop_24h', 'SCOP 24 uur'),
             ('sensor.openquatt_scop_7d', 'SCOP 7 dagen'),
             ('sensor.openquatt_scop_30d', 'SCOP 30 dagen'),
             ('sensor.openquatt_scop_lifetime', 'SCOP totaal')),
        graph(168,
              ('sensor.openquatt_scop_24h', 'SCOP 24 uur'),
              title='Rendement, week'),
    ),
    badges=[
        badge('sensor.openquatt_electrical_energy_daily', 'Vandaag'),
        badge('sensor.openquatt_heatpump_cop_daily', 'COP'),
        badge('sensor.openquatt_scop_24h', 'SCOP 24u'),
    ],
)

# --------------------------------------------------------------------------
# VIEW 6 — AFSTELLEN
# --------------------------------------------------------------------------
V_TUNE = view(
    'Afstellen', 'afstellen', 'mdi:tune',
    grid(
        head('mdi:file-compare', 'Vangnet'),
        # Bovenaan met opzet: dit is de plek waar je ziet dat er iets is
        # veranderd dat je niet zelf hebt gedaan.
        rows(('binary_sensor.openquatt_instellingen_gewijzigd', 'Afgeweken'),
             ('sensor.openquatt_instellingen_afwijking', 'Wat er afwijkt'),
             ('sensor.openquatt_instellingen_afwijkingen', 'Aantal'),
             ('button.openquatt_instellingen_vastleggen', 'Vastleggen als ijkpunt'),
             ('button.openquatt_instellingen_herstellen', 'Herstellen')),
    ),
    grid(
        head('mdi:water-boiler', 'Hoe warm wil ik water'),
        rows(('number.openquatt_dhw_start_top', 'Start onder'),
             ('number.openquatt_dhw_hp_stop_top', 'HP stopt bij tanktop'),
             ('number.openquatt_dhw_hp_stop_tank_bottom', 'HP stopt bij tankbodem'),
             ('number.openquatt_dhw_boost_target', 'Element tot'),
             ('number.openquatt_dhw_legionella_target', 'Legionella')),
    ),
    grid(
        head('mdi:stairs-up', 'Wanneer mag de tweede HP'),
        rows(('switch.openquatt_dhw_single_hp_mode', 'Single-HP mode'),
             ('number.openquatt_dhw_single_hp_level_bump', 'Level bump'),
             ('switch.openquatt_dhw_second_hp_assist', 'Assist toestaan'),
             ('number.openquatt_dhw_assist_max_level', 'Assist max niveau'),
             ('number.openquatt_dhw_assist_min_tank_rise', 'Min stijging tanktop'),
             ('number.openquatt_dhw_assist_max_discharge_temp', 'Persgas grens'),
             ('number.openquatt_dhw_assist_max_water_out', 'Water-uit grens'),
             ('number.openquatt_dhw_assist_tank_bottom_stop', 'Staart: bodem'),
             ('number.openquatt_dhw_soft_start_step_time', 'Zachte aanloop')),
    ),
    grid(
        head('mdi:shield-alert', 'Hoe streng zijn de beveiligingen'),
        rows('Condensordruk',
             ('switch.openquatt_pressure_protection_enable', 'Aan'),
             ('number.openquatt_pressure_soft_cap', 'Zacht'),
             ('number.openquatt_pressure_hard_cap', 'Hard'),
             ('number.openquatt_pressure_soft_cap_step_time', 'Tijd per stap'),
             'Aanvoertemperatuur',
             ('switch.openquatt_supply_temp_protection_enable', 'Aan'),
             ('number.openquatt_supply_temp_soft_cap', 'Zacht'),
             ('number.openquatt_supply_temp_hard_cap', 'Hard'),
             'Persgas',
             ('switch.openquatt_discharge_protection_enable', 'Aan'),
             ('number.openquatt_discharge_soft_cap', 'Zacht'),
             ('number.openquatt_discharge_hard_cap', 'Hard')),
    ),
    grid(
        head('mdi:robot', 'Wat mag automatisch'),
        rows(('switch.openquatt_dhw_auto_boost_enable', 'Automatische boost'),
             ('switch.openquatt_dhw_solar_boost_auto', 'Trigger: goedkoop tarief'),
             ('switch.openquatt_dhw_pv_self_consumption_enable', 'Trigger: PV-export'),
             ('number.openquatt_dhw_pv_boost_export_threshold', 'PV-drempel'),
             ('switch.openquatt_dhw_tapdetectie', 'Tapdetectie'),
             ('number.openquatt_dhw_tapdetectie_drempel', 'Tapdrempel')),
    ),
    grid(
        head('mdi:restart', 'Cycling'),
        rows(('number.openquatt_cycling_waarschuwing_2u', 'Drempel 2 uur'),
             ('number.openquatt_cycling_waarschuwing_72u', 'Drempel 72 uur'),
             ('button.openquatt_cycling_waarschuwing_bevestigen', 'Incident bevestigen')),
    ),
)

# --------------------------------------------------------------------------
# VIEW 7 — UITZOEKEN
# --------------------------------------------------------------------------
V_DIAG = view(
    'Uitzoeken', 'uitzoeken', 'mdi:stethoscope',
    grid(
        head('mdi:restart', 'Compressor cycling'),
        rows(('sensor.openquatt_cycling_status', 'Status'),
             ('binary_sensor.openquatt_cycling_afwisselend_pendelen', 'Afwisselend pendelen'),
             'Starts',
             ('sensor.openquatt_hp1_starts_2u', 'HP1 2u'),
             ('sensor.openquatt_hp2_starts_2u', 'HP2 2u'),
             ('sensor.openquatt_hp1_starts_24u', 'HP1 24u'),
             ('sensor.openquatt_hp2_starts_24u', 'HP2 24u'),
             ('sensor.openquatt_hp1_starts_72u', 'HP1 72u'),
             ('sensor.openquatt_hp2_starts_72u', 'HP2 72u'),
             'Piek tijdens incident',
             ('sensor.openquatt_cycling_piek_2u', '2 uur'),
             ('sensor.openquatt_cycling_piek_72u', '72 uur')),
    ),
    grid(
        head('mdi:stairs-down', 'Compressorbegrenzing'),
        rows('Actieve cap (10 = geen)',
             ('sensor.openquatt_pressure_hp1_level_cap', 'Druk HP1'),
             ('sensor.openquatt_pressure_hp2_level_cap', 'Druk HP2'),
             ('sensor.openquatt_supply_temp_hp1_level_cap', 'Aanvoer HP1'),
             ('sensor.openquatt_supply_temp_hp2_level_cap', 'Aanvoer HP2'),
             ('sensor.openquatt_discharge_hp1_level_cap', 'Persgas HP1'),
             ('sensor.openquatt_discharge_hp2_level_cap', 'Persgas HP2'),
             'Hoe vaak vandaag',
             ('sensor.openquatt_pressure_soft_events_24h', 'Druk zacht'),
             ('sensor.openquatt_supply_temp_soft_events_24h', 'Aanvoer zacht'),
             ('sensor.openquatt_discharge_soft_events_24h', 'Persgas zacht'),
             'Persgas piek',
             ('sensor.openquatt_discharge_hp1_peak', 'HP1'),
             ('sensor.openquatt_discharge_hp2_peak', 'HP2')),
    ),
    grid(
        head('mdi:thermometer-lines', 'Koudemiddel'),
        rows(('sensor.openquatt_hp1_suction_superheat', 'HP1 superheat'),
             ('sensor.openquatt_hp2_suction_superheat', 'HP2 superheat'),
             ('sensor.openquatt_hp1_discharge_superheat', 'HP1 persgas-superheat'),
             ('sensor.openquatt_hp2_discharge_superheat', 'HP2 persgas-superheat'),
             ('sensor.openquatt_hp1_evaporator_pressure', 'HP1 verdamperdruk'),
             ('sensor.openquatt_hp2_evaporator_pressure', 'HP2 verdamperdruk'),
             ('sensor.openquatt_hp1_condenser_pressure', 'HP1 condensordruk'),
             ('sensor.openquatt_hp2_condenser_pressure', 'HP2 condensordruk')),
    ),
    grid(
        head('mdi:memory', 'Geheugen'),
        rows(('sensor.openquatt_intern_geheugen_vrij', 'Intern vrij'),
             ('sensor.openquatt_intern_geheugen_grootste_blok', 'Grootste blok'),
             ('sensor.openquatt_intern_geheugen_laagste_stand', 'Laagste stand'),
             ('sensor.openquatt_psram_vrij', 'PSRAM vrij'),
             ('sensor.openquatt_psram_totaal', 'PSRAM totaal')),
        graph(168,
              ('sensor.openquatt_intern_geheugen_vrij', 'Vrij'),
              ('sensor.openquatt_intern_geheugen_laagste_stand', 'Laagste stand'),
              title='Intern geheugen, week'),
    ),
    grid(
        head('mdi:tag-text', 'Systeem'),
        rows(('sensor.openquatt_openquatt_version', 'Versie'),
             ('sensor.openquatt_openquatt_release_channel', 'Kanaal'),
             ('sensor.openquatt_uptime', 'Uptime'),
             ('sensor.openquatt_ip_address', 'IP'),
             ('button.openquatt_restart', 'Herstart'),
             ('button.openquatt_safe_mode', 'Safe mode')),
    ),
)


def collect(o, acc):
    if isinstance(o, dict):
        if isinstance(o.get('entity'), str):
            acc.append(o['entity'])
        for v in o.values():
            collect(v, acc)
    elif isinstance(o, list):
        for v in o:
            collect(v, acc)


def main():
    views = [V_NU, V_DHW, V_HEAT, V_COOL, V_ENERGY, V_TUNE, V_DIAG]
    used = []
    collect(views, used)
    unknown = sorted({e for e in used if e not in ENTS})
    print('entiteitverwijzingen: {}  uniek: {}'.format(len(used), len(set(used))))
    if unknown:
        print('\nONBEKEND ({}):'.format(len(unknown)))
        for u in unknown:
            print('   ', u)
        return 1
    print('alle verwijzingen bestaan in de firmware')

    bar = '# ' + '=' * 78
    header_lines = [
        bar,
        '# OpenQuatt - NL Duo dashboard (V2)',
        bar,
        '#',
        '# Ingedeeld naar taak in plaats van naar firmware-module. De oude indeling',
        '# volgde de YAML-bestandsstructuur -- Sensorconfiguratie, Flow,',
        '# Warmteregeling -- en dat is hoe de code is opgebouwd, niet hoe je hem',
        '# gebruikt.',
        '#',
        '# Drie diepten:',
        '#   Nu          is alles goed?',
        '#   Domein      wat doet het, en waar draai ik aan?',
        '#   Uitzoeken   waarom doet het dat?',
        '#',
        '# De sectie Aandacht op de eerste tab werkt met voorwaardelijke kaarten:',
        '# die verschijnen alleen als er iets is. Een lege sectie is het signaal dat',
        '# alles in orde is -- niet een scherm vol groene vinkjes.',
        '#',
        '# VEREIST FIRMWARE v0.60.0 OF NIEUWER.',
        '#',
        '# Dit dashboard verwijst naar entiteiten uit de cycling monitor (v0.58) en',
        '# het instellingen-vangnet (v0.60). Draait er oudere firmware op het',
        '# apparaat, dan tonen die vakjes "Entiteit niet gevonden" -- en belangrijker:',
        '# de rustmelding op de eerste tab verschijnt dan nooit, omdat haar conditie',
        '# aan twee van die entiteiten hangt.',
        '#',
        '# Gegenereerd. Elke entity-id is gecontroleerd tegen de entiteiten die de',
        '# firmware-BRON aanmaakt. Dat is niet hetzelfde als wat er op het apparaat',
        '# draait; flash eerst, importeer daarna.',
        bar,
        '',
    ]
    body = yaml.dump({'title': 'OpenQuatt', 'views': views}, allow_unicode=True,
                     sort_keys=False, default_flow_style=False, width=100)
    target = os.environ.get('OQ_DASH_OUT', OUT)
    io.open(target, 'w', encoding='utf-8', newline='').write('\n'.join(header_lines) + body)
    print('geschreven naar', target)
    return 0


if __name__ == '__main__':
    sys.exit(main())
