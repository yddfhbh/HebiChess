/* Presentation-only welcome flow. Existing app.js owns lobby/game lifecycle. */
(() => {
  const welcome = document.getElementById('welcome');
  const game = document.getElementById('game');
  if (!welcome || !game) return;
  const leaveWelcome = () => {
    document.body.classList.remove('ui-welcome');
    welcome.hidden = true;
  };
  if (new URLSearchParams(location.search).has('game')) leaveWelcome();
  else document.body.classList.add('ui-welcome');
  document.getElementById('welcome-start')?.addEventListener('click', leaveWelcome);
  new MutationObserver(() => { if (!game.hidden) leaveWelcome(); })
    .observe(game, { attributes: true, attributeFilter: ['hidden'] });
})();
