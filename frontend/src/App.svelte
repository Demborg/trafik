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
    type: string
    lines: LineView[]
  }

  let groups: GroupView[] = $state([])
  let error: string | null = $state(null)
  let loading = $state(true)
  let nextRefreshIn: number | null = $state(null)
  let now = $state(Math.floor(Date.now() / 1000))  // unix seconds, updated every second

  function minutesUntil(ts: number): number {
    return Math.floor((ts - now) / 60)
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

  function scheduleRefresh(seconds: number) {
    nextRefreshIn = seconds
    const interval = setInterval(() => {
      nextRefreshIn = (nextRefreshIn ?? 0) - 1
      if (nextRefreshIn <= 0) {
        clearInterval(interval)
        nextRefreshIn = null
        fetchDepartures()
      }
    }, 1000)
  }

  onMount(() => {
    setInterval(() => { now = Math.floor(Date.now() / 1000) }, 1000)
    fetchDepartures()
  })

  function formatType(t: string) {
    return t.charAt(0).toUpperCase() + t.slice(1).toLowerCase()
  }
</script>

<main>
  <h1>Bagarmossen <span class="clock">{formatTime(now)}</span></h1>
  {#if loading}
    <p>Loading…</p>
  {:else if error}
    <p class="error">{error}</p>
  {:else}
    {#each groups as group}
      <section>
        <h2>{formatType(group.type)}</h2>
        {#each group.lines as l}
          {#each l.destinations as dest}
            {@const upcoming = dest.departures.map(minutesUntil).filter(m => m >= 0)}
            {#if upcoming.length > 0}
              <div class="row">
                <span class="line">{l.line}</span>
                <span class="destination">{dest.destination}</span>
                <span class="times">{upcoming.join(', ')} min</span>
              </div>
            {/if}
          {/each}
        {/each}
      </section>
    {/each}
  {/if}
  {#if nextRefreshIn !== null}
    <p class="refresh">refresh in {nextRefreshIn}s</p>
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
  h2 {
    font-size: 0.85rem;
    text-transform: uppercase;
    letter-spacing: 0.1em;
    color: #888;
    margin: 1.5rem 0 0.25rem;
    border-bottom: 1px solid #ddd;
    padding-bottom: 0.25rem;
  }
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
</style>
