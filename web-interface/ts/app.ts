async function pingSession() {
  try {
    await fetch('/session/ping', {
      method: 'POST',
      cache: 'no-store'
    });
  } catch (err) {
    // Ignore network errors during transitions.
  }
}

pingSession();
setInterval(pingSession, 15000);
