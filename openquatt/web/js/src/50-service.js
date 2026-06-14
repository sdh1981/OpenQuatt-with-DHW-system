  // ============================================================
  // Service view — Temperatuursensor kalibratie wizard
  // ============================================================

  // Derive wizard step (0/1/2) from firmware state text.
  function calWizardStep() {
    const txt = String(getEntityValue("calStateText") || "").toLowerCase();
    if (txt.startsWith("meting klaar") || txt.startsWith("klaar")) return 2;
    if (txt.startsWith("meting bezig") || txt.startsWith("fout")) return 1;
    return 0;
  }

  function calFmt(key, decimals = 2, fallback = "—") {
    const v = getEntityNumericValue(key);
    if (Number.isNaN(v)) return fallback;
    return v.toFixed(decimals) + " °C";
  }

  function calOffsetBadge(value) {
    if (Number.isNaN(value)) return `<span class="oq-cal-offset-badge oq-cal-offset-badge--zero">—</span>`;
    const sign = value >= 0 ? "+" : "";
    const cls = Math.abs(value) < 0.005
      ? "oq-cal-offset-badge--zero"
      : value > 0
        ? "oq-cal-offset-badge--pos"
        : "oq-cal-offset-badge--neg";
    return `<span class="oq-cal-offset-badge ${cls}">${sign}${value.toFixed(2)} °C</span>`;
  }

  function renderCalStepIndicator(activeStep) {
    const steps = [
      { label: "Voorbereiding" },
      { label: "Meting" },
      { label: "Offsets toepassen" },
    ];
    return `
      <div class="oq-cal-steps">
        ${steps.map((s, i) => {
          const cls = i < activeStep ? "oq-cal-step--done"
            : i === activeStep ? "oq-cal-step--active"
            : "oq-cal-step--pending";
          const inner = i < activeStep
            ? `<svg viewBox="0 0 24 24" width="14" height="14" fill="currentColor"><path d="M9,20.42L2.79,14.21L5.62,11.38L9,14.77L18.88,4.88L21.71,7.71L9,20.42Z"/></svg>`
            : String(i + 1);
          const connector = i < steps.length - 1
            ? `<div style="flex:1;height:1px;background:var(--oq-helper-line-strong);margin:0 8px;"></div>`
            : "";
          return `
            <div class="oq-cal-step ${cls}" style="display:flex;align-items:center;gap:8px;">
              <div class="oq-cal-step-circle">${inner}</div>
              <span class="oq-cal-step-label">${escapeHtml(s.label)}</span>
            </div>
            ${connector}
          `;
        }).join("")}
      </div>
    `;
  }

  function renderCalBodyStep0() {
    return `
      <div class="oq-cal-body">
        <div class="oq-cal-info-box">
          <p>Zorg dat het systeem niet aan het verwarmen of koelen is.
          Klik op <strong>Kalibratie starten</strong> — de firmware zet de CM Override automatisch op
          <em>Force CM1</em> zodat de pomp circuleert zonder compressor.
          Wacht daarna tot alle 4 sensoren dezelfde temperatuur meten.</p>
        </div>
        ${renderCalActiveOffsets()}
      </div>
    `;
  }

  function renderCalBodyStep1() {
    const stateText = getEntityValue("calStateText") || "Meting bezig";
    const remaining = getEntityNumericValue("calRemaining");
    const maxDur = getEntityNumericValue("calMaxDuration") || 300;
    const elapsed = Number.isNaN(remaining) ? 0 : Math.max(0, maxDur - remaining);
    const pct = Number.isNaN(remaining) ? 0 : Math.min(100, (elapsed / maxDur) * 100);

    const spread = getEntityNumericValue("calSpread");
    const threshold = getEntityNumericValue("calSpreadThreshold") || 0.25;
    const spreadOk = !Number.isNaN(spread) && spread < threshold;

    const remainingLabel = Number.isNaN(remaining)
      ? ""
      : ` - max. ${Math.ceil(remaining)} s resterend`;

    return `
      <div class="oq-cal-body">
        <div class="oq-cal-status-box oq-cal-status-box--measuring">
          <div class="oq-cal-status-box-title">Meting bezig${remainingLabel}</div>
          <div class="oq-cal-status-box-copy">
            De waterpomp circuleert zonder compressor. De firmware stopt zodra het laatste
            meetvenster binnen de spreiding- en driftgrenzen valt.
          </div>
          <span class="oq-cal-status-badge ${spreadOk ? "oq-cal-status-badge--ok" : "oq-cal-status-badge--waiting"}">
            ${spreadOk ? "Binnen grenzen" : "Nog niet binnen grenzen"}
          </span>
          <div class="oq-cal-progress-wrap">
            <div class="oq-cal-progress-bar" style="width:${pct.toFixed(1)}%"></div>
          </div>
        </div>

        <div class="oq-cal-readings">
          <div class="oq-cal-reading">
            <div class="oq-cal-reading-label">HP1 water in</div>
            <div class="oq-cal-reading-value">${calFmt("calHp1InRaw")}</div>
          </div>
          <div class="oq-cal-reading">
            <div class="oq-cal-reading-label">HP1 water uit</div>
            <div class="oq-cal-reading-value">${calFmt("calHp1OutRaw")}</div>
          </div>
          <div class="oq-cal-reading">
            <div class="oq-cal-reading-label">HP2 water in</div>
            <div class="oq-cal-reading-value">${calFmt("calHp2InRaw")}</div>
          </div>
          <div class="oq-cal-reading">
            <div class="oq-cal-reading-label">HP2 water uit</div>
            <div class="oq-cal-reading-value">${calFmt("calHp2OutRaw")}</div>
          </div>
        </div>

        <div class="oq-cal-meta-row">
          <div class="oq-cal-meta-item${!Number.isNaN(spread) && spread >= threshold ? " oq-cal-meta-item--highlight" : ""}">
            <div class="oq-cal-meta-label">Spreiding</div>
            <div class="oq-cal-meta-value">${calFmt("calSpread")}</div>
          </div>
          <div class="oq-cal-meta-item">
            <div class="oq-cal-meta-label">Supply verschil</div>
            <div class="oq-cal-meta-value">${calFmt("calSupplyDiff")}</div>
          </div>
        </div>

        <p class="oq-cal-supply-note">Supply wordt alleen als diagnose getoond en niet automatisch gecorrigeerd.</p>
      </div>
    `;
  }

  function renderCalBodyStep2() {
    const ref     = getEntityNumericValue("calReference");
    const supDiff = getEntityNumericValue("calSupplyDiff");
    const spread  = getEntityNumericValue("calSpread");

    const rows = [
      { label: "HP1 water in",  rawKey: "calHp1InRaw",  propKey: "calPropHp1In",  offKey: "calOffsetHp1In" },
      { label: "HP1 water uit", rawKey: "calHp1OutRaw", propKey: "calPropHp1Out", offKey: "calOffsetHp1Out" },
      { label: "HP2 water in",  rawKey: "calHp2InRaw",  propKey: "calPropHp2In",  offKey: "calOffsetHp2In" },
      { label: "HP2 water uit", rawKey: "calHp2OutRaw", propKey: "calPropHp2Out", offKey: "calOffsetHp2Out" },
    ];

    const spreadLabel = Number.isNaN(spread) ? "" : ` — spreiding ${spread.toFixed(2)} °C`;

    return `
      <div class="oq-cal-body">
        <div class="oq-cal-status-box oq-cal-status-box--done">
          <div class="oq-cal-status-box-title">Meting klaar${spreadLabel}</div>
          <div class="oq-cal-status-box-copy">Controleer de voorgestelde offsets en pas ze toe.</div>
          <span class="oq-cal-status-badge oq-cal-status-badge--ok">Resultaat beschikbaar</span>
        </div>

        <div class="oq-cal-chips">
          <span class="oq-cal-chip">Referentie ${Number.isNaN(ref) ? "—" : ref.toFixed(2) + " °C"}</span>
          <span class="oq-cal-chip">Supply verschil ${Number.isNaN(supDiff) ? "—" : supDiff.toFixed(2) + " °C"}</span>
        </div>

        <table class="oq-cal-table">
          <thead>
            <tr>
              <th>Sensor</th>
              <th>Raw gemiddelde</th>
              <th>Huidig actief</th>
              <th>Voorstel</th>
              <th>Na toepassen</th>
            </tr>
          </thead>
          <tbody>
            ${rows.map(row => {
              const raw    = getEntityNumericValue(row.rawKey);
              const prop   = getEntityNumericValue(row.propKey);
              const active = getEntityNumericValue(row.offKey);
              const after  = !Number.isNaN(raw) && !Number.isNaN(prop) ? (raw + prop).toFixed(2) + " °C" : "—";
              return `
                <tr>
                  <td>${escapeHtml(row.label)}</td>
                  <td>${Number.isNaN(raw)    ? "—" : raw.toFixed(2)    + " °C"}</td>
                  <td>${Number.isNaN(active) ? "—" : active.toFixed(2) + " °C"}</td>
                  <td>${calOffsetBadge(prop)}</td>
                  <td>${after}</td>
                </tr>
              `;
            }).join("")}
          </tbody>
        </table>

        <p class="oq-cal-supply-note">Supply wordt alleen als diagnose getoond en niet automatisch gecorrigeerd.</p>
      </div>
    `;
  }

  function renderCalActiveOffsets() {
    const sensors = [
      { label: "HP1 water in",  offKey: "calOffsetHp1In",  liveKey: "hp1WaterIn"  },
      { label: "HP1 water uit", offKey: "calOffsetHp1Out", liveKey: "hp1WaterOut" },
      { label: "HP2 water in",  offKey: "calOffsetHp2In",  liveKey: "hp2WaterIn"  },
      { label: "HP2 water uit", offKey: "calOffsetHp2Out", liveKey: "hp2WaterOut" },
    ];

    const anyNonZero = sensors.some(s => {
      const v = getEntityNumericValue(s.offKey);
      return !Number.isNaN(v) && Math.abs(v) > 0.005;
    });

    // Check if we have any live readings to show
    const anyLive = sensors.some(s => !Number.isNaN(getEntityNumericValue(s.liveKey)));

    return `
      <div class="oq-cal-offsets-info">
        <div class="oq-cal-offsets-info-title">Actieve kalibratie-offsets</div>

        ${anyLive ? `
          <table class="oq-cal-table" style="margin-bottom:12px;">
            <thead>
              <tr>
                <th>Sensor</th>
                <th>Ruw (nu)</th>
                <th>Gecalibreerd (nu)</th>
                <th>Offset</th>
              </tr>
            </thead>
            <tbody>
              ${sensors.map(s => {
                const cal    = getEntityNumericValue(s.liveKey);
                const offset = getEntityNumericValue(s.offKey);
                const raw    = (!Number.isNaN(cal) && !Number.isNaN(offset)) ? cal - offset : NaN;
                const rawTxt = Number.isNaN(raw)    ? "—" : raw.toFixed(2)    + " °C";
                const calTxt = Number.isNaN(cal)    ? "—" : cal.toFixed(2)    + " °C";
                return `
                  <tr>
                    <td>${escapeHtml(s.label)}</td>
                    <td style="color:var(--oq-helper-faint)">${rawTxt}</td>
                    <td style="font-weight:600">${calTxt}</td>
                    <td>${calOffsetBadge(Number.isNaN(offset) ? NaN : offset)}</td>
                  </tr>
                `;
              }).join("")}
            </tbody>
          </table>
        ` : `
          <div class="oq-cal-offsets-grid" style="margin-bottom:8px;">
            ${sensors.map(s => {
              const v = getEntityNumericValue(s.offKey);
              const label = Number.isNaN(v) ? "—" : (v >= 0 ? "+" : "") + v.toFixed(2) + " °C";
              return `
                <div class="oq-cal-offset-row">
                  <span class="oq-cal-offset-row-label">${escapeHtml(s.label)}</span>
                  <span class="oq-cal-offset-row-value">${label}</span>
                </div>
              `;
            }).join("")}
          </div>
        `}

        ${anyNonZero ? `
          <button class="oq-cal-reset-link" type="button" data-oq-action="cal-reset">
            Alle offsets terugzetten naar 0
          </button>
        ` : ""}
      </div>
    `;
  }

  function renderCalFooter(step) {
    const busy = state.busyAction && state.busyAction.startsWith("cal");
    if (step === 0) {
      return `
        <div class="oq-cal-footer-status">
          <div class="oq-cal-footer-status-label">Huidige status</div>
          <div class="oq-cal-footer-status-copy">
            Gereed. Druk op 'Kalibratie starten' om te beginnen.
          </div>
        </div>
        <div class="oq-cal-footer">
          <button class="oq-cal-btn oq-cal-btn--primary" type="button"
            data-oq-action="cal-start" ${busy ? "disabled" : ""}>
            Kalibratie starten
          </button>
        </div>
      `;
    }
    if (step === 1) {
      const statusDetail = getEntityValue("calStatusDetail") || "De pomp draait en de firmware wacht op een stabiel temperatuurbeeld.";
      return `
        <div class="oq-cal-footer-status">
          <div class="oq-cal-footer-status-label">Huidige status</div>
          <div class="oq-cal-footer-status-copy">${escapeHtml(statusDetail)}</div>
        </div>
        <div class="oq-cal-footer">
          <button class="oq-cal-btn oq-cal-btn--secondary" type="button"
            data-oq-action="cal-stop" ${busy ? "disabled" : ""}>
            Meting stoppen
          </button>
          <button class="oq-cal-btn oq-cal-btn--primary" type="button"
            data-oq-action="cal-apply" disabled>
            Offsets toepassen
          </button>
        </div>
      `;
    }
    // step 2
    return `
      <div class="oq-cal-footer-status">
        <div class="oq-cal-footer-status-label">Huidige status</div>
        <div class="oq-cal-footer-status-copy">
          Klaar om toe te passen. Controleer de voorgestelde offsets voordat je ze toepast.
        </div>
      </div>
      <div class="oq-cal-footer">
        <button class="oq-cal-btn oq-cal-btn--secondary" type="button"
          data-oq-action="cal-start" ${busy ? "disabled" : ""}>
          Kalibratie starten
        </button>
        <button class="oq-cal-btn oq-cal-btn--primary" type="button"
          data-oq-action="cal-apply" ${busy ? "disabled" : ""}>
          Offsets toepassen
        </button>
      </div>
    `;
  }

  function renderServiceView() {
    const step = calWizardStep();

    const bodyHtml = step === 2 ? renderCalBodyStep2()
      : step === 1 ? renderCalBodyStep1()
      : renderCalBodyStep0();

    return `
      <section class="oq-cal-panel">
        <p class="oq-cal-kicker">Service</p>
        <h2 class="oq-cal-title">Temperatuursensoren kalibreren</h2>
        <p class="oq-cal-subtitle">Laat de waterpomp draaien zonder compressor en bepaal offsets voor HP1/HP2 water in/out.</p>

        <div class="oq-cal-wizard">
          <div class="oq-cal-wizard-head">
            <h3>Temperatuursensoren kalibreren</h3>
            <p>Doorloop voorbereiding, meting en toepassen in vaste volgorde.
               De meting stopt eerder zodra de sensoren stabiel genoeg zijn.</p>
            <p class="oq-cal-note">De voorgestelde waarden worden pas actief wanneer je ze toepast; supply blijft een diagnosewaarde.</p>
            ${renderCalStepIndicator(step)}
          </div>

          ${bodyHtml}

          ${renderCalFooter(step)}
        </div>
      </section>
    `;
  }
