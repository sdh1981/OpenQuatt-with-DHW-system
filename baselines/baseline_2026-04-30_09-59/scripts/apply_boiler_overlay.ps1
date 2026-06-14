param(
  [string]$Root = (Resolve-Path "$PSScriptRoot\..").Path
)

$overlay = Join-Path $Root "openquatt\overlays\lilygo_cic_boiler"
if (-not (Test-Path $overlay)) {
  throw "Overlay map niet gevonden: $overlay"
}

$map = @(
  @{ From = "openquatt_base.yaml"; To = "openquatt_base.yaml" },
  @{ From = "oq_boiler_control.yaml"; To = "openquatt\oq_boiler_control.yaml" },
  @{ From = "oq_dhw_modbus_bridge.yaml"; To = "openquatt\oq_dhw_modbus_bridge.yaml" },
  @{ From = "oq_dhw_controller_logic.h"; To = "openquatt\includes\oq_dhw_controller_logic.h" },
  @{ From = "oq_supervisory_controlmode.yaml"; To = "openquatt\oq_supervisory_controlmode.yaml" },
  @{ From = "oq_thermal_request_control.yaml"; To = "openquatt\oq_thermal_request_control.yaml" },
  @{ From = "oq_substitutions_common.yaml"; To = "openquatt\oq_substitutions_common.yaml" },
  @{ From = "oq_substitutions_lilygo_tconnect.yaml"; To = "openquatt\profiles\oq_substitutions_lilygo_tconnect.yaml" },
  @{ From = "oq_substitutions_lilygo_tconnect_cic.yaml"; To = "openquatt\profiles\oq_substitutions_lilygo_tconnect_cic.yaml" }
)

foreach ($item in $map) {
  $src = Join-Path $overlay $item.From
  $dst = Join-Path $Root $item.To
  if (-not (Test-Path $src)) {
    throw "Overlay bronbestand ontbreekt: $src"
  }
  Copy-Item $src $dst -Force
  Write-Host "Applied: $($item.To)"
}

Write-Host "Boiler overlay actief."
