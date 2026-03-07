<script lang="ts">
  import { onMount } from 'svelte'

  const BACKEND_URL = import.meta.env.VITE_BACKEND_URL

  interface Departure {
    line: string
    destination: string
    minutes: number
    transport_type: string
  }

  let departures: Departure[] = $state([])
  let error: string | null = $state(null)
  let loading = $state(true)

  onMount(async () => {
    try {
      const res = await fetch(`${BACKEND_URL}/departures`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()
      departures = data.departures
    } catch (e) {
      error = String(e)
    } finally {
      loading = false
    }
  })
</script>

<main>
  <h1>Bagarmossen</h1>
  {#if loading}
    <p>Loading…</p>
  {:else if error}
    <p class="error">{error}</p>
  {:else}
    <ul>
      {#each departures as d}
        <li>
          <span class="line">{d.line}</span>
          <span class="destination">{d.destination}</span>
          <span class="minutes">{d.minutes} min</span>
        </li>
      {/each}
    </ul>
  {/if}
</main>

<style>
  main {
    font-family: monospace;
    max-width: 480px;
    margin: 2rem auto;
    padding: 1rem;
  }
  ul { list-style: none; padding: 0; }
  li { display: flex; gap: 1rem; padding: 0.5rem 0; border-bottom: 1px solid #eee; }
  .line { font-weight: bold; width: 2rem; }
  .minutes { margin-left: auto; }
  .error { color: red; }
</style>
