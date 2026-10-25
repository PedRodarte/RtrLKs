/* Lógica comum às páginas: pega a localização (se permitida) e abre o WhatsApp com a
   mensagem pronta. Se a localização for negada ou falhar, manda só o texto.

   URL do QR (montada pela tag):
     ?t=5531999998888   WhatsApp do responsável (55 + DDD + número). Sem ele, o WhatsApp
                        deixa escolher o contato (bom para testar mandando para você mesmo).
     ?d=FindLK01        identificação da tag; não entra na mensagem */
(function () {
  "use strict";

  const params = new URLSearchParams(location.search);
  const TELEFONE = (params.get("t") || "").replace(/\D/g, "").slice(0, 15);

  /* Formatação do WhatsApp: *negrito*, _itálico_, "> " no início da linha = citação.
     O link fica sozinho e sem formatação na linha, para continuar clicável. */
  const TITULO = "🚨 *CRIANÇA ENCONTRADA!* 🚨\n\n";
  const AGUARDO_E_TEMPO_REAL =
    "⏳ *Vou aguardar aqui!*\n\n" +
    "Vou tentar compartilhar minha localização em tempo real para te ajudar a nos encontrar:\n" +
    "> 📎 → _Localização_ → _Localização em tempo real_ → _1 hora_";

  const STATUS = {
    buscando: "Pegando a localização… se o celular perguntar, toque em Permitir.",
    ok: "Localização encontrada. Abrindo o WhatsApp…",
    sem_gps: "Sem localização. Abrindo o WhatsApp com o que você escreveu…",
  };

  let ultimaUrl = "";

  function descricao(texto) {
    const t = (texto || "").trim();
    return t ? "📝 _" + t.replace(/[_*~]/g, "") + "_\n" : "";
  }

  function comLocalizacao(lat, lon, texto) {
    // Sem "origin": o Maps traça a rota a partir de onde estiver quem abrir o link
    const rota = "https://www.google.com/maps/dir/?api=1&destination=" + lat + "," + lon;
    return TITULO + "Encontrei essa criança perdida! Segue a nossa localização:\n" +
      "📍 " + rota + "\n" + descricao(texto) + "\n" + AGUARDO_E_TEMPO_REAL;
  }

  function soTexto(texto) {
    const d = descricao(texto);
    return TITULO + "Encontrei essa criança perdida! " +
      (d ? "Estamos aqui:\n" + d : "Me liga para combinarmos onde nos encontrar.\n") +
      "\n" + AGUARDO_E_TEMPO_REAL;
  }

  function abrir(mensagem) {
    const base = TELEFONE ? "https://wa.me/" + TELEFONE : "https://wa.me/";
    ultimaUrl = base + "?text=" + encodeURIComponent(mensagem);
    location.href = ultimaUrl; // navegação, não pop-up: funciona mesmo depois da espera do GPS
  }

  /* onStatus(codigo, texto): "buscando", "ok" ou "sem_gps" */
  function enviar(textoDescricao, onStatus) {
    const avisar = (codigo) => onStatus && onStatus(codigo, STATUS[codigo]);
    if (!("geolocation" in navigator) || !window.isSecureContext) {
      avisar("sem_gps");
      abrir(soTexto(textoDescricao));
      return;
    }
    avisar("buscando");
    navigator.geolocation.getCurrentPosition(
      (pos) => {
        avisar("ok");
        abrir(comLocalizacao(pos.coords.latitude.toFixed(6), pos.coords.longitude.toFixed(6), textoDescricao));
      },
      () => {
        avisar("sem_gps");
        abrir(soTexto(textoDescricao));
      },
      { enableHighAccuracy: true, timeout: 15000, maximumAge: 30000 }
    );
  }

  /* Telefone para exibir e ligar: 5531999998888 -> "(31) 99999-8888" e "tel:+5531999998888".
     Retorna null se a URL não tiver telefone. */
  function telefone() {
    if (!TELEFONE) {
      return null;
    }
    let exibir = "+" + TELEFONE;
    const br = TELEFONE.match(/^55(\d{2})(\d{4,5})(\d{4})$/);
    if (br) {
      exibir = "(" + br[1] + ") " + br[2] + "-" + br[3];
    }
    return { exibir, link: "tel:+" + TELEFONE };
  }

  window.FindLK = { enviar, ultimaUrl: () => ultimaUrl, telefone };
})();
