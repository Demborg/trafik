<script lang="ts">
  import { onMount } from 'svelte'

  const BACKEND_URL = import.meta.env.VITE_BACKEND_URL

  interface DestinationView {
    destination: string
    departures: number[]  // unix timestamps
  }

  interface LineView {
    line: string
    destinations: DestinationView[]
  }

  interface GroupView {
    label: string
    lines: LineView[]
  }

  interface BatteryPoint {
    v_bat: number
    p_bat: number
    timestamp: string
  }

  let groups: GroupView[] = $state([])
  let batteryPoints: BatteryPoint[] = $state([])
  let batteryDays = $state(1)
  let weather: string | null = $state(null)
  let error: string | null = $state(null)
  let loading = $state(true)
  let nextRefreshIn: number | null = $state(null)
  let now = $state(Math.floor(Date.now() / 1000))  // unix seconds, updated every second

  function minutesUntil(ts: number): number {
    return Math.floor((ts - now) / 60)
  }

  function formatDepartures(minutes: number[]): string {
    if (minutes.length === 0) return ''
    const hasNow = minutes[0] === 0
    const rest = hasNow ? minutes.slice(1) : minutes
    if (hasNow && rest.length === 0) return 'Nu'
    if (hasNow) return `Nu, ${rest.join(', ')} min`
    return `${minutes.join(', ')} min`
  }

  function formatTime(unixSec: number): string {
    const d = new Date(unixSec * 1000)
    return d.toLocaleTimeString('sv-SE', { hour: '2-digit', minute: '2-digit' })
  }

  async function fetchDepartures() {
    loading = true
    try {
      const res = await fetch(`${BACKEND_URL}/departures`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()
      groups = data.groups
      weather = data.weather ?? null
      error = null
      if (data.suggested_sleep_seconds) {
        scheduleRefresh(data.suggested_sleep_seconds)
      }
    } catch (e) {
      error = String(e)
    } finally {
      loading = false
    }
  }

  async function fetchBattery() {
    try {
      const res = await fetch(`${BACKEND_URL}/battery?days=${batteryDays}`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      batteryPoints = await res.json()
    } catch (e) {
      console.error('Failed to fetch battery:', e)
    }
  }

  // Reactive Sparkline points using Svelte 5 runes
  const sparklinePoints = $derived.by(() => {
    if (batteryPoints.length < 2) return ""
    
    // Fixed 0-100% Y axis
    const yMin = 0
    const yMax = 100
    const yRange = yMax - yMin

    return batteryPoints
      .map((p, i) => {
        const x = (i / (batteryPoints.length - 1)) * 400
        const y = 60 - ((p.p_bat - yMin) / yRange) * 60
        return `${x},${y}`
      })
      .join(' ')
  })

  function scheduleRefresh(seconds: number) {
    nextRefreshIn = seconds
    const interval = setInterval(() => {
      nextRefreshIn = (nextRefreshIn ?? 0) - 1
      if (nextRefreshIn <= 0) {
        clearInterval(interval)
        nextRefreshIn = null
        fetchDepartures()
        fetchBattery()
      }
    }, 1000)
  }

  onMount(() => {
    setInterval(() => { now = Math.floor(Date.now() / 1000) }, 1000)
    fetchDepartures()
    fetchBattery()
  })
</script>

<main>
  <h1>Bagarmossen <span class="clock">{formatTime(now)}</span></h1>
  {#if weather !== null}
    <p class="weather">{weather}</p>
  {/if}
  {#if loading}
    <p>Laddar…</p>
  {:else if error}
    <p class="error">{error}</p>
  {:else}
    {#each groups as group}
      <section>
        <h2>{group.label}</h2>
        {#each group.lines as l}
          {#each l.destinations as dest}
            {@const upcoming = dest.departures.map(minutesUntil).filter(m => m >= 0)}
            {#if upcoming.length > 0}
              <div class="row">
                <span class="line">{l.line}</span>
                <span class="destination">{dest.destination}</span>
                <span class="times">{formatDepartures(upcoming)}</span>
              </div>
            {/if}
          {/each}
        {/each}
      </section>
    {/each}

    {#if batteryPoints.length > 0}
      <section class="battery">
        <div class="header-with-controls">
          <h2>Batteri</h2>
          <div class="controls">
            {#each [1, 2, 7, 30] as d}
              <button 
                class:active={batteryDays === d} 
                onclick={() => { batteryDays = d; fetchBattery(); }}
              >
                {d === 1 ? '24h' : d === 2 ? '48h' : d + 'd'}
              </button>
            {/each}
          </div>
        </div>
        <div class="battery-status">
          <span>{batteryPoints[batteryPoints.length - 1].p_bat}%</span>
          <span class="voltage">{batteryPoints[batteryPoints.length - 1].v_bat.toFixed(2)}V</span>
        </div>
        <svg viewBox="0 0 400 60" class="sparkline">
          <polyline
            fill="none"
            stroke="#888"
            stroke-width="1.5"
            stroke-linejoin="round"
            points={sparklinePoints}
          />
        </svg>
      </section>
    {/if}
  {/if}
  {#if nextRefreshIn !== null}
    <p class="refresh">uppdatering om {nextRefreshIn}s</p>
  {/if}
</main>

<style>
  main {
    font-family: monospace;
    max-width: 480px;
    margin: 2rem auto;
    padding: 1rem;
  }
  h1 {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
  }
  .clock { font-size: 1rem; font-weight: normal; color: #555; }
  .weather { font-size: 0.85rem; color: #666; margin: -0.75rem 0 0.5rem; }
  h2 {
    font-size: 0.85rem;
    text-transform: uppercase;
    letter-spacing: 0.1em;
    color: #888;
    margin: 1.5rem 0 0.25rem;
    padding-bottom: 0.25rem;
  }
  section h2 { border-bottom: 1px solid #ddd; }
  .header-with-controls h2 { border-bottom: none; margin: 0; }
  .row {
    display: flex;
    gap: 1rem;
    padding: 0.3rem 0;
    border-bottom: 1px solid #f0f0f0;
  }
  .line { font-weight: bold; width: 2rem; flex-shrink: 0; }
  .destination { flex: 1; }
  .times { margin-left: auto; white-space: nowrap; }
  .error { color: red; }
  .refresh { font-size: 0.75rem; color: #aaa; margin-top: 1.5rem; }

  .battery { margin-top: 1.5rem; }
  .header-with-controls {
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid #ddd;
    margin-top: 1.5rem;
  }
  .header-with-controls h2 { padding: 0.5rem 0; }
  .controls { display: flex; gap: 0.5rem; }
  .controls button {
    background: none;
    border: 1px solid #eee;
    font-family: monospace;
    font-size: 0.7rem;
    padding: 2px 6px;
    cursor: pointer;
    color: #888;
  }
  .controls button.active {
    background: #888;
    color: white;
    border-color: #888;
  }
  .battery-status {
    display: flex;
    justify-content: space-between;
    font-size: 0.85rem;
    margin-bottom: 0.25rem;
  }
  .voltage { color: #888; }
  .sparkline {
    width: 100%;
    height: 40px;
    background: #f9f9f9;
    border: 1px solid #eee;
    padding: 4px;
  }
</style>
