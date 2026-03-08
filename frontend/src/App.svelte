<script lang="ts">
  import { onMount } from 'svelte'

  const BACKEND_URL = import.meta.env.VITE_BACKEND_URL

  interface DestinationView {
    destination: string
    minutes: number[]
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

  onMount(async () => {
    try {
      const res = await fetch(`${BACKEND_URL}/departures`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()
      groups = data.groups
    } catch (e) {
      error = String(e)
    } finally {
      loading = false
    }
  })

  function formatType(t: string) {
    return t.charAt(0).toUpperCase() + t.slice(1).toLowerCase()
  }
</script>

<main>
  <h1>Bagarmossen</h1>
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
            <div class="row">
              <span class="line">{l.line}</span>
              <span class="destination">{dest.destination}</span>
              <span class="times">{dest.minutes.join(', ')} min</span>
            </div>
          {/each}
        {/each}
      </section>
    {/each}
  {/if}
</main>

<style>
  main {
    font-family: monospace;
    max-width: 480px;
    margin: 2rem auto;
    padding: 1rem;
  }
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
</style>
