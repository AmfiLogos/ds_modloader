// GET /privacy (rewritten to /api/privacy). Same text as crashrep/PRIVACY.md.
const HTML = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Crash reports and privacy</title>
<style>
body{font-family:system-ui,sans-serif;max-width:40rem;margin:2rem auto;padding:0 1rem;line-height:1.5;color:#1a1a1a;background:#fff}
@media (prefers-color-scheme: dark){body{color:#e8e8e8;background:#161616}}
h1{font-size:1.5rem}h2{font-size:1.15rem;margin-top:1.6rem}
</style>
</head>
<body>
<h1>Dungeon Settlers Modded: crash reports and privacy</h1>
<p>This page tells you what a crash report contains. It also tells you how to turn crash reports off.</p>

<h2>What we collect</h2>
<p>A crash report can contain:</p>
<ul>
<li>The loader log (<code>dsload.log</code>).</li>
<li>The game logs: <code>player.log</code> and the game's <code>ErrorLog.txt</code>.</li>
<li>The multiplayer log (<code>rig.log</code>).</li>
<li>Summaries of desync reports.</li>
<li>The versions of the loader, of your mods and of the game build.</li>
<li>Your operating system and your CPU type.</li>
<li>A random install id. The loader makes this id. It is not a hardware id.</li>
</ul>
<p>We do not collect a hardware id.</p>

<h2>What is removed before sending</h2>
<p>Your computer removes these items before it sends the report:</p>
<ul>
<li>User names in folder paths, and your user name, full name and computer name anywhere in the text.</li>
<li>Steam names and Steam ids.</li>
<li>Player names.</li>
<li>IP addresses.</li>
<li>Email addresses and network card (MAC) addresses.</li>
</ul>

<h2>Why we collect it</h2>
<p>We use the reports to find and fix crashes, freezes, desyncs, failed joins and an empty game view.</p>

<h2>When it is sent</h2>
<ul>
<li>Only after you click Yes.</li>
<li>Only after a crash, a freeze, a desync report, a failed join, or a multiplayer view that showed nothing.</li>
<li>At most 3 reports per day.</li>
<li>The report is sent at the next start of the game, or at the title screen. Never while you play.</li>
</ul>

<h2>How long we keep it</h2>
<p>We keep each report for 90 days. Then it is deleted automatically.</p>

<h2>Your IP address</h2>
<p>The server does not store your IP address with the report.</p>

<h2>How to turn it off</h2>
<ol>
<li>Go to the title screen.</li>
<li>Open Mods.</li>
<li>Turn off the switch "Send crash reports".</li>
</ol>

<p>This page uses no tracking and no cookies.</p>
</body>
</html>
`;

export function GET() {
  return new Response(HTML, {
    status: 200,
    headers: {
      'content-type': 'text/html; charset=utf-8',
      'cache-control': 'public, max-age=3600',
      'x-content-type-options': 'nosniff',
    },
  });
}
