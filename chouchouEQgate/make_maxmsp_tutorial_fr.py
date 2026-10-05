#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tutoriel PDF français — objets MaxMSP du EQ Gate spectral."""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.platypus import (
    HRFlowable,
    ListFlowable,
    ListItem,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)

# Sortie principale (copie aussi vers MAX/eq gate au runtime)
WORK = Path(__file__).resolve().parent
OUT_WORK = WORK / "chouchouEQGate_MaxMSP_Tutoriel_Objets_FR.pdf"
# The MAX folder sits three levels above this plugin folder (next to the JUCE folder).
OUT_MAX = WORK.parents[2] / "MAX" / "eq gate" / "chouchouEQGate_MaxMSP_Tutoriel_Objets_FR.pdf"

INK = colors.HexColor("#1a1a1a")
MUTED = colors.HexColor("#555555")
ACCENT = colors.HexColor("#1e4d6b")
RULE = colors.HexColor("#c8c8c8")
SOFT = colors.HexColor("#f3f6f8")
HEADER_BG = colors.HexColor("#1e4d6b")
CODE_BG = colors.HexColor("#eef2f4")
ROLE_BG = colors.HexColor("#e8f0e9")


def styles():
    base = getSampleStyleSheet()
    return {
        "cover_brand": ParagraphStyle(
            "cover_brand", fontName="Helvetica-Bold", fontSize=26,
            textColor=ACCENT, alignment=TA_CENTER, spaceAfter=6, leading=32,
        ),
        "cover_title": ParagraphStyle(
            "cover_title", fontName="Helvetica", fontSize=15,
            textColor=INK, alignment=TA_CENTER, spaceAfter=6, leading=20,
        ),
        "cover_sub": ParagraphStyle(
            "cover_sub", fontName="Helvetica-Oblique", fontSize=10,
            textColor=MUTED, alignment=TA_CENTER, spaceAfter=4, leading=14,
        ),
        "h1": ParagraphStyle(
            "h1", fontName="Helvetica-Bold", fontSize=13.5,
            textColor=ACCENT, spaceBefore=14, spaceAfter=7, leading=17,
        ),
        "h2": ParagraphStyle(
            "h2", fontName="Helvetica-Bold", fontSize=11,
            textColor=INK, spaceBefore=10, spaceAfter=5, leading=14,
        ),
        "obj": ParagraphStyle(
            "obj", fontName="Courier-Bold", fontSize=12,
            textColor=ACCENT, spaceBefore=12, spaceAfter=4, leading=15,
        ),
        "body": ParagraphStyle(
            "body", fontName="Helvetica", fontSize=9.2,
            textColor=INK, alignment=TA_JUSTIFY, spaceAfter=5, leading=12.8,
        ),
        "bullet": ParagraphStyle(
            "bullet", fontName="Helvetica", fontSize=9.2,
            textColor=INK, spaceAfter=2, leading=12.5,
        ),
        "label": ParagraphStyle(
            "label", fontName="Helvetica-Bold", fontSize=8.5,
            textColor=ACCENT, spaceBefore=4, spaceAfter=2, leading=11,
        ),
        "official": ParagraphStyle(
            "official", fontName="Helvetica-Oblique", fontSize=8.8,
            textColor=INK, alignment=TA_JUSTIFY, spaceAfter=5, leading=12,
            leftIndent=4, rightIndent=4, backColor=SOFT,
        ),
        "code": ParagraphStyle(
            "code", fontName="Courier", fontSize=8.5,
            textColor=INK, alignment=TA_LEFT, spaceAfter=4, leading=11.5,
            backColor=CODE_BG,
        ),
        "caption": ParagraphStyle(
            "caption", fontName="Helvetica-Oblique", fontSize=8,
            textColor=MUTED, alignment=TA_CENTER, spaceBefore=2, spaceAfter=8, leading=10,
        ),
        "th": ParagraphStyle(
            "th", fontName="Helvetica-Bold", fontSize=8, textColor=colors.white, leading=10.5,
        ),
        "td": ParagraphStyle(
            "td", fontName="Helvetica", fontSize=8, textColor=INK, leading=10.5,
        ),
        "toc": ParagraphStyle(
            "toc", fontName="Helvetica", fontSize=9.5,
            textColor=INK, spaceAfter=3, leading=13, leftIndent=8,
        ),
    }


def footer(canvas, doc):
    canvas.saveState()
    page = canvas.getPageNumber()
    if page > 1:
        w, _ = A4
        canvas.setStrokeColor(RULE)
        canvas.setLineWidth(0.4)
        canvas.line(2 * cm, 1.35 * cm, w - 2 * cm, 1.35 * cm)
        canvas.setFont("Helvetica", 7.5)
        canvas.setFillColor(MUTED)
        canvas.drawString(2 * cm, 0.85 * cm, "chouchouEQGate · MaxMSP")
        canvas.drawCentredString(w / 2, 0.85 * cm, f"Tutoriel objets — {page}")
        canvas.drawRightString(w - 2 * cm, 0.85 * cm, "FR")
    canvas.restoreState()


def bullets(items, st):
    return ListFlowable(
        [ListItem(Paragraph(t, st["bullet"]), leftIndent=10, bulletColor=ACCENT) for t in items],
        bulletType="bullet", start="•", leftIndent=16, bulletFontSize=8, spaceAfter=6,
    )


def table(headers, rows, widths, st):
    data = [[Paragraph(h, st["th"]) for h in headers]]
    for r in rows:
        data.append([Paragraph(c, st["td"]) for c in r])
    t = Table(data, colWidths=widths)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HEADER_BG),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, SOFT]),
        ("GRID", (0, 0), (-1, -1), 0.35, RULE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 5),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5),
        ("TOPPADDING", (0, 0), (-1, -1), 3.5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3.5),
    ]))
    return t


def object_block(st, name, official, principle, usage, role, docs_url=None):
    """Bloc pédagogique standard pour un objet."""
    bits = [Paragraph(name, st["obj"])]
    bits.append(HRFlowable(width="100%", thickness=0.5, color=RULE, spaceBefore=0, spaceAfter=4))
    bits.append(Paragraph("Selon la documentation Cycling '74", st["label"]))
    bits.append(Paragraph(official, st["official"]))
    bits.append(Paragraph("Principe", st["label"]))
    bits.append(Paragraph(principle, st["body"]))
    bits.append(Paragraph("Usage (général)", st["label"]))
    bits.append(Paragraph(usage, st["body"]))
    bits.append(Paragraph("Rôle dans le EQ Gate", st["label"]))
    bits.append(Paragraph(f'<font color="#1a4a2a"><b>{role}</b></font>', st["body"]))
    if docs_url:
        bits.append(Paragraph(f'Doc officielle : <font face="Courier" size="8">{docs_url}</font>', st["caption"]))
    return bits


def build():
    st = styles()
    doc = SimpleDocTemplate(
        str(OUT_WORK),
        pagesize=A4,
        leftMargin=1.9 * cm,
        rightMargin=1.9 * cm,
        topMargin=1.6 * cm,
        bottomMargin=1.9 * cm,
        title="chouchouEQGate — Tutoriel MaxMSP (objets)",
        author="chouchou",
        subject="Guide pédagogique des objets MaxMSP du gate spectral",
    )
    story = []

    # —— Couverture ——
    story.append(Spacer(1, 2.8 * cm))
    story.append(Paragraph("chouchou", st["cover_brand"]))
    story.append(HRFlowable(width="35%", thickness=1.2, color=ACCENT, spaceBefore=2, spaceAfter=12, hAlign="CENTER"))
    story.append(Paragraph("EQ Gate — Version MaxMSP / Max for Live", st["cover_title"]))
    story.append(Paragraph(
        "Tutoriel détaillé des objets<br/>principe · usage · documentation officielle · rôle dans le gate",
        st["cover_sub"],
    ))
    story.append(Spacer(1, 1.2 * cm))
    story.append(Paragraph("Fichiers : EQ gate.maxpat  +  fftin.maxpat", st["cover_sub"]))
    story.append(Paragraph("pfft~ fftin 2048 4  ·  stéréo  ·  logique Gate", st["cover_sub"]))
    story.append(Paragraph("Références : docs.cycling74.com  ·  2026", st["cover_sub"]))
    story.append(PageBreak())

    # —— Sommaire ——
    story.append(Paragraph("Sommaire", st["h1"]))
    for line in [
        "1. Architecture du patch (vue d'ensemble)",
        "2. Chaîne du signal — étape par étape",
        "3. Patch parent — EQ gate.maxpat",
        "4. Sous-patch FFT — fftin.maxpat (DSP)",
        "5. Contrôles (in / route / clip / maximum)",
        "6. Formule complète et valeurs par défaut",
        "7. Exercices d'écoute et bonnes pratiques",
        "8. Liens documentation Cycling '74",
    ]:
        story.append(Paragraph(line, st["toc"]))
    story.append(Spacer(1, 0.4 * cm))
    story.append(Paragraph(
        "Ce document est conçu comme un <b>cours</b> : pour chaque objet, vous trouverez "
        "d'abord la formulation proche de la doc officielle, puis le principe, l'usage général, "
        "et enfin son rôle précis dans <i>ce</i> gate spectral.",
        st["body"],
    ))

    # —— 1. Architecture ——
    story.append(Paragraph("1. Architecture du patch", st["h1"]))
    story.append(Paragraph(
        "Le projet est séparé en <b>deux fichiers obligatoires dans le même dossier</b> :",
        st["body"],
    ))
    story.append(bullets([
        "<font face='Courier'><b>EQ gate.maxpat</b></font> — interface : entrées audio Live/Max, "
        "quatre flonum (paramètres), objet <font face='Courier'>pfft~</font>, sorties.",
        "<font face='Courier'><b>fftin.maxpat</b></font> — sous-patch chargé par "
        "<font face='Courier'>pfft~</font> : tout le traitement spectral bin par bin.",
    ], st))
    story.append(Paragraph(
        "Pourquoi deux fichiers ? Parce que <font face='Courier'>pfft~</font> charge un patcher "
        "par <b>nom</b> (ici <font face='Courier'>fftin</font>, sans extension dans l'argument). "
        "Si <font face='Courier'>fftin.maxpat</font> n'est pas à côté, Max ne trouve pas le sous-patch.",
        st["body"],
    ))
    story.append(table(
        ["Couche", "Fichier", "Responsabilité"],
        [
            ["Host / UI", "EQ gate.maxpat", "plugin~ → pfft~ → plugout~ ; flonums → inlets 3–6"],
            ["STFT", "pfft~ (moteur)", "fenêtres, overlap, FFT/IFFT, OLA"],
            ["DSP spectral", "fftin.maxpat", "magnitude → gain → phase → reconstruction"],
        ],
        [3.2 * cm, 4.2 * cm, 8.8 * cm],
        st,
    ))
    story.append(Paragraph("Trois couches : interface, moteur STFT, logique de gate", st["caption"]))

    # —— 2. Chaîne ——
    story.append(Paragraph("2. Chaîne du signal — étape par étape", st["h1"]))
    story.append(Paragraph(
        "Pour <b>chaque canal</b> (L et R sont identiques et parallèles) :",
        st["body"],
    ))
    story.append(Paragraph(
        "plugin~  →  pfft~  →  [fftin~ → cartopol~ → rampsmooth~ → +~ε → /~ → pow~ → clip~ 0..1 "
        "→ *~ mag → poltocar~ → fftout~]  →  plugout~",
        st["code"],
    ))
    story.append(table(
        ["Étape", "Objets", "Ce qui se passe"],
        [
            ["1", "fftin~", "Signal temporel → spectre (réel / imaginaire)"],
            ["2", "cartopol~", "Réel/imag → magnitude + phase"],
            ["3", "rampsmooth~", "Lisse la magnitude (attack / release)"],
            ["4", "+~ ε", "Évite la division par zéro"],
            ["5", "/~", "magnitude / threshold  (rapport)"],
            ["6", "pow~", "élève le rapport à la puissance Amount"],
            ["7", "clip~ 0 1", "mode Gate : gain ≤ 1"],
            ["8", "*~", "gain × magnitude originale (non lissée)"],
            ["9", "poltocar~", "nouvelle mag + phase d'origine → réel/imag"],
            ["10", "fftout~", "IFFT + fenêtre → signal temporel"],
        ],
        [1.4 * cm, 3.6 * cm, 11.2 * cm],
        st,
    ))
    story.append(Paragraph(
        "La <b>phase</b> contourne toute la chaîne de gain : elle part de "
        "<font face='Courier'>cartopol~</font> directement vers "
        "<font face='Courier'>poltocar~</font>. On ne modifie que l'amplitude — "
        "c'est la base d'un traitement spectral « propre ».",
        st["body"],
    ))
    story.append(PageBreak())

    # —— 3. Parent ——
    story.append(Paragraph("3. Patch parent — EQ gate.maxpat", st["h1"]))

    story.extend(object_block(
        st,
        "flonum",
        "Number box flottant : affiche et envoie des nombres à virgule. "
        "On peut fixer minimum / maximum / format d'affichage.",
        "C'est une interface de contrôle Max (pas un objet audio ~). "
        "Quand vous changez la valeur, le outlet gauche envoie le nombre.",
        "Double-cliquer pour taper ; glisser verticalement pour ajuster. "
        "Ici : threshold 0.001–0.5, amount 0.1–4, attack 0.1–200, release 1–500.",
        "Quatre flonums pilotent les inlets 3–6 de pfft~ (donc in 3–6 dans fftin). "
        "Pas de loadbang : les défauts DSP sont dans les arguments des objets "
        "(/~ 0.01, pow~ 0.8, rampsmooth~ 5. 80.).",
        "https://docs.cycling74.com/reference/flonum/",
    ))

    story.extend(object_block(
        st,
        "plugin~",
        "Reçoit l'audio stéréo depuis l'hôte (Max for Live / plugin Max).",
        "Dans un device Audio Effect, plugin~ est l'entrée du signal venant de Live "
        "(ou du DAW). Deux outlets signal = gauche et droite.",
        "Placer en début de chaîne ; connecter L/R vers les deux premiers inlets de pfft~.",
        "Source audio du gate. Sans plugin~, pas d'entrée depuis Live.",
        "https://docs.cycling74.com/reference/plugin~/",
    ))

    story.extend(object_block(
        st,
        "pfft~ fftin 2048 4",
        "Gestionnaire de traitement spectral : effectue FFT/IFFT, fenêtrage, "
        "chevauchement et overlap-add. Charge un sous-patcher nommé (ici fftin).",
        "Arguments : nom du patch, taille FFT (2048), facteur d'overlap (4 → hop = 2048/4 = 512, "
        "soit 75 % de chevauchement). Le nombre d'inlets dépend des fftin~/in du sous-patch ; "
        "le nombre d'outlets dépend des fftout~/out.",
        "Créer fftin.maxpat dans le même dossier. Écrire pfft~ fftin 2048 4 "
        "(sans .maxpat). Les messages sur les inlets ≥ 3 vont vers les objets in N.",
        "Cœur du système : encapsule tout le DSP spectral. Ici 6 inlets "
        "(2 audio + 4 paramètres) et 2 outlets audio stéréo.",
        "https://docs.cycling74.com/reference/pfft~/",
    ))

    story.extend(object_block(
        st,
        "plugout~",
        "Renvoie l'audio stéréo vers l'hôte (sortie du device).",
        "Symétrique de plugin~ : deux inlets signal → sortie Live/DAW.",
        "Connecter les deux outlets de pfft~ aux deux inlets de plugout~.",
        "Sortie finale du gate traité.",
        "https://docs.cycling74.com/reference/plugout~/",
    ))

    story.append(Paragraph("Schéma parent (résumé)", st["h2"]))
    story.append(Paragraph(
        "flonums ──► pfft~ inlets 3–6\n"
        "plugin~ L/R ──► pfft~ inlets 1–2 ──► plugout~ L/R",
        st["code"],
    ))
    story.append(PageBreak())

    # —— 4. DSP objects ——
    story.append(Paragraph("4. Sous-patch FFT — fftin.maxpat (DSP)", st["h1"]))
    story.append(Paragraph(
        "Tout ce qui suit existe en double (canal L à gauche, R à droite), sauf la "
        "colonne centrale des contrôles partagés.",
        st["body"],
    ))

    story.extend(object_block(
        st,
        "fftin~ 1  /  fftin~ 2",
        "Entrée signal d'un patcher chargé par pfft~. Applique la fenêtre et effectue la FFT. "
        "Outlets : réel, imaginaire, et (souvent) index de bin / sync.",
        "Le numéro (1, 2…) correspond à l'inlet signal du pfft~ parent. "
        "Par défaut : fenêtre Hanning. pfft~ gère l'overlap ; fftin~ fait la transformée.",
        "fftin~ 1 pour la gauche, fftin~ 2 pour la droite. Connecter réel→cartopol~ gauche, "
        "imag→cartopol~ droite.",
        "Porte d'entrée spectrale. Transforme le temps en spectre complexe pour chaque frame.",
        "https://docs.cycling74.com/reference/fftin~/",
    ))

    story.extend(object_block(
        st,
        "cartopol~",
        "Conversion cartésienne → polaire : réel/imaginaire → magnitude (amplitude) et phase (radians).",
        "Pour chaque bin : mag = sqrt(re²+im²), phase = atan2(im, re). "
        "Si seule la magnitude est connectée, le calcul de phase peut être allégé.",
        "Inlets : réel, imaginaire. Outlets : magnitude, phase (−π…π).",
        "Sépare « combien d'énergie » (mag) et « où dans le cycle » (phase). "
        "La mag alimente le gate ; la phase est préservée jusqu'à poltocar~.",
        "https://docs.cycling74.com/reference/cartopol~/",
    ))

    story.extend(object_block(
        st,
        "rampsmooth~ 5. 80.",
        "Lisse un signal en limitant la vitesse de montée et de descente (rampes linéaires en samples).",
        "Deux paramètres : temps de montée (attack) et de descente (release), en nombre de samples "
        "du domaine où l'objet tourne (ici : domaine FFT / vecteur spectral). "
        "Évite les sauts brutaux de gain d'une frame à l'autre.",
        "Arguments initiaux 5. et 80. (noter le point décimal Max). "
        "Inlets 2 et 3 reçoivent attack / release depuis in 5 et in 6.",
        "Enveloppe temporelle du gate spectral : empêche le scintillement (chatter) "
        "quand un bin traverse le seuil.",
        "https://docs.cycling74.com/reference/rampsmooth~/",
    ))

    story.extend(object_block(
        st,
        "+~ 0.00000001",
        "Addition signal : ajoute une constante (ou un second signal) à l'entrée.",
        "Ici on ajoute un epsilon minuscule à la magnitude lissée.",
        "+~ suivi d'une très petite constante (1e-8).",
        "Sécurité numérique avant /~ : empêche mag≈0 de produire Inf/NaN.",
        "https://docs.cycling74.com/reference/+~/",
    ))

    story.extend(object_block(
        st,
        "/~ 0.01",
        "Division signal : inlet gauche ÷ inlet droit (ou ÷ argument).",
        "Calcule le rapport magnitude / threshold. Si mag &gt; seuil, rapport &gt; 1 ; sinon &lt; 1.",
        "Argument 0.01 = seuil par défaut tant que flonum n'a pas envoyé de valeur. "
        "L'inlet droit reçoit threshold (après maximum).",
        "Normalisation par rapport au seuil — première étape de la formule de gain.",
        "https://docs.cycling74.com/reference//~/",
    ))

    story.extend(object_block(
        st,
        "pow~ 0.8",
        "Puissance signal : base^exposant. L'exposant peut être un argument ou l'inlet droit.",
        "Applique gain_raw = (mag/seuil)^amount. Ce n'est pas un ratio en dB : c'est un exposant. "
        "Amount élevé = courbe plus agressive.",
        "Défaut 0.8. Inlet droit = amount (via clip 0.1–4).",
        "Contrôle la « dureté » du gate. Cœur non linéaire de l'effet.",
        "https://docs.cycling74.com/reference/pow~/",
    ))

    story.extend(object_block(
        st,
        "clip~ 0. 1.",
        "Limite un signal entre un minimum et un maximum.",
        "Toute valeur &lt; 0 → 0 ; &gt; 1 → 1. Transforme l'expanseur potentiel en gate "
        "(on n'amplifie jamais au-dessus de l'unité).",
        "clip~ 0. 1. juste après pow~.",
        "Définit le mode Gate de cette version Max : pas de boost des pics, seulement atténuation.",
        "https://docs.cycling74.com/reference/clip~/",
    ))

    story.extend(object_block(
        st,
        "*~",
        "Multiplication signal (gain / VCA spectral).",
        "gain_limité × magnitude_originale (sortie directe de cartopol~, non lissée).",
        "Inlet 0 = gain (après clip~), inlet 1 = mag brute.",
        "Applique le masque de gain à l'amplitude réelle du bin. "
        "Le lissage agit sur la détection ; la mag multipliée reste celle de la frame courante "
        "(réactivité + stabilité).",
        "https://docs.cycling74.com/reference/*~/",
    ))

    story.extend(object_block(
        st,
        "poltocar~",
        "Conversion polaire → cartésienne : magnitude + phase → réel / imaginaire.",
        "Inverse de cartopol~. Nécessaire avant l'IFFT (fftout~ attend un spectre complexe).",
        "Inlets : nouvelle magnitude, phase d'origine. Outlets : réel, imaginaire.",
        "Recompose le spectre modifié pour la resynthèse, phase intacte.",
        "https://docs.cycling74.com/reference/poltocar~/",
    ))

    story.extend(object_block(
        st,
        "fftout~ 1  /  fftout~ 2",
        "Sortie d'un patcher pfft~ : IFFT, fenêtre, overlap-add vers le parent.",
        "Le numéro correspond à l'outlet du pfft~. Reçoit réel et imaginaire (et sync si besoin).",
        "Connecter poltocar~ réel/imag → fftout~.",
        "Porte de sortie : reconstruit le signal temporel stéréo vers plugout~.",
        "https://docs.cycling74.com/reference/fftout~/",
    ))
    story.append(PageBreak())

    # —— 5. Contrôles ——
    story.append(Paragraph("5. Contrôles — in / route / maximum / clip", st["h1"]))
    story.append(Paragraph(
        "Les paramètres ne passent pas par send/receive (s/r) : ils entrent par des "
        "objets <font face='Courier'>in</font> numérotés, pour éviter conflits et messages parasites.",
        st["body"],
    ))

    story.extend(object_block(
        st,
        "in 3 / in 4 / in 5 / in 6",
        "Entrée de messages pour un patcher chargé par pfft~ ou poly~. "
        "Le numéro N crée l'inlet N sur l'objet parent.",
        "Attention : fftin~ 1 et 2 occupent déjà la logique audio des inlets 1–2. "
        "Les paramètres commencent donc à in 3.",
        "in 3 = threshold, in 4 = amount, in 5 = attack, in 6 = release.",
        "Pont entre les flonums du parent et le DSP du sous-patch.",
        "https://docs.cycling74.com/reference/in/",
    ))

    story.extend(object_block(
        st,
        "route all",
        "Route les messages selon le premier élément. Ici, l'argument all attire "
        "les messages préfixés « all » vers le outlet correspondant.",
        "Dans Live / Max for Live, certains messages système (dont all) peuvent arriver "
        "sur les inlets et provoquer l'erreur « bad number » si un nombre est attendu. "
        "route all filtre : le outlet « match » reçoit le reste du message all ; "
        "le outlet de droite (non-match) laisse passer les vrais nombres des flonums.",
        "Placer juste après chaque in N ; connecter le outlet de droite (non-all) "
        "vers maximum / clip / rampsmooth~.",
        "Pare-feu anti-messages parasites — stabilité du device dans Live.",
        "https://docs.cycling74.com/reference/route/",
    ))

    story.extend(object_block(
        st,
        "maximum 0.000001",
        "Renvoie le maximum entre l'entrée et une constante (ou second inlet). "
        "Note : l'objet s'appelle maximum, pas max (max est réservé).",
        "Garantit que le threshold ne descend jamais à 0 (division sûre).",
        "Après route all (thresh) → maximum 0.000001 → inlet droit de /~.",
        "Sécurité du seuil : évite /0 même si l'utilisateur met une valeur extrême.",
        "https://docs.cycling74.com/reference/maximum/",
    ))

    story.extend(object_block(
        st,
        "clip 0.1 4.  (contrôle, sans ~)",
        "Version message de clip : borne un nombre entre min et max.",
        "Différent de clip~ (audio). Ici on borne Amount avant pow~.",
        "clip 0.1 4. après route amount.",
        "Protège pow~ d'exposants absurdes (trop faibles ou trop violents).",
        "https://docs.cycling74.com/reference/clip/",
    ))

    story.append(Paragraph("Carte des inlets pfft~", st["h2"]))
    story.append(table(
        ["Inlet pfft~", "Objet dans fftin", "Paramètre", "Destination DSP"],
        [
            ["1", "fftin~ 1", "Audio L", "chaîne gauche"],
            ["2", "fftin~ 2", "Audio R", "chaîne droite"],
            ["3", "in 3", "Threshold", "/~ (inlet droit)"],
            ["4", "in 4", "Amount", "pow~ (inlet droit)"],
            ["5", "in 5", "Attack", "rampsmooth~ inlet 2"],
            ["6", "in 6", "Release", "rampsmooth~ inlet 3"],
        ],
        [2.4 * cm, 2.8 * cm, 3.2 * cm, 7.8 * cm],
        st,
    ))
    story.append(PageBreak())

    # —— 6. Formule ——
    story.append(Paragraph("6. Formule complète et valeurs par défaut", st["h1"]))
    story.append(Paragraph("Pour chaque bin de chaque canal :", st["body"]))
    story.append(Paragraph(
        "mag, phase = cartopol(re, im)\n"
        "mag_s = rampsmooth(mag, attack, release)\n"
        "ratio = (mag_s + ε) / max(threshold, 1e-6)\n"
        "gain = clip( ratio ^ amount , 0, 1 )\n"
        "mag_out = gain * mag\n"
        "re', im' = poltocar(mag_out, phase)",
        st["code"],
    ))
    story.append(table(
        ["Paramètre", "Défaut dans les objets", "Plage flonum", "Effet à l'oreille"],
        [
            ["Threshold", "/~ 0.01", "0.001 – 0.5", "plus bas = plus de nettoyage"],
            ["Amount", "pow~ 0.8", "0.1 – 4", "plus haut = contraste plus dur"],
            ["Attack", "rampsmooth~ 5.", "0.1 – 200", "montée du gain plus/moins rapide"],
            ["Release", "rampsmooth~ 80.", "1 – 500", "descente plus/moins douce"],
        ],
        [3 * cm, 4.5 * cm, 3.5 * cm, 5.2 * cm],
        st,
    ))
    story.append(Paragraph(
        "Rappel pédagogique : Amount ≠ ratio de compresseur en dB. "
        "C'est un exposant. Threshold n'est pas un seuil de gate large bande en dBFS "
        "sur tout le signal, mais un seuil de magnitude par bin FFT.",
        st["body"],
    ))

    # —— 7. Exercices ——
    story.append(Paragraph("7. Exercices d'écoute (comme en cours)", st["h1"]))
    story.append(Paragraph("Exercice A — Isoler le seuil", st["h2"]))
    story.append(bullets([
        "Amount ≈ 1, Attack/Release moyens.",
        "Balayer Threshold du haut vers le bas : entendre le fond disparaître bande par bande.",
        "Noter la zone où le corps du son reste intact.",
    ], st))
    story.append(Paragraph("Exercice B — Isoler Amount", st["h2"]))
    story.append(bullets([
        "Fixer un Threshold confortable.",
        "Passer Amount de 0.3 → 2 → 4 : observer le passage « subtil → chirurgical → agressif ».",
    ], st))
    story.append(Paragraph("Exercice C — Artefacts temporels", st["h2"]))
    story.append(bullets([
        "Attack et Release très courts : chercher le scintillement spectral.",
        "Allonger Release : le nettoyage devient plus musical.",
        "Comprendre que rampsmooth~ est votre « anti-aliasing temporel » du gate.",
    ], st))
    story.append(Paragraph("Exercice D — Lire le patch", st["h2"]))
    story.append(bullets([
        "Ouvrir fftin.maxpat et suivre un seul canal (L) avec le doigt.",
        "Vérifier que la phase saute de cartopol~ à poltocar~ sans passer par pow~.",
        "Vérifier qu'un seul maximum / clip sert les deux canaux (contrôles partagés).",
    ], st))

    # —— 8. Liens ——
    story.append(Paragraph("8. Liens documentation Cycling '74", st["h1"]))
    story.append(Paragraph(
        "Base : <font face='Courier'>https://docs.cycling74.com/reference/</font> "
        "+ nom de l'objet (ex. <font face='Courier'>pfft~/</font>).",
        st["body"],
    ))
    story.append(table(
        ["Objet", "URL de référence"],
        [
            ["pfft~", "https://docs.cycling74.com/reference/pfft~/"],
            ["fftin~ / fftout~", "https://docs.cycling74.com/reference/fftin~/  ·  …/fftout~/"],
            ["cartopol~ / poltocar~", "https://docs.cycling74.com/reference/cartopol~/  ·  …/poltocar~/"],
            ["rampsmooth~", "https://docs.cycling74.com/reference/rampsmooth~/"],
            ["pow~ / clip~ / *~ / /~", "https://docs.cycling74.com/reference/pow~/ etc."],
            ["in / route / maximum / clip", "https://docs.cycling74.com/reference/in/ etc."],
            ["Tutoriel STFT Max", "MSP Analysis Tutorial 4 — Signal Processing with pfft~"],
        ],
        [4.5 * cm, 11.7 * cm],
        st,
    ))
    story.append(Spacer(1, 0.6 * cm))
    story.append(HRFlowable(width="100%", thickness=0.6, color=RULE, spaceBefore=4, spaceAfter=8))
    story.append(Paragraph(
        "Tutoriel rédigé pour le patch chouchou EQ Gate (MaxMSP / M4L) · "
        "Fichiers : EQ gate.maxpat + fftin.maxpat · Logique Gate (gain clipé 0–1).<br/>"
        "Les citations « documentation officielle » sont des reformulations pédagogiques "
        "inspirées des pages Cycling '74 ; consultez toujours la doc en ligne pour le détail exhaustif.",
        st["caption"],
    ))

    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(f"OK work: {OUT_WORK}")

    # Copie vers le dossier Max
    try:
        OUT_MAX.parent.mkdir(parents=True, exist_ok=True)
        OUT_MAX.write_bytes(OUT_WORK.read_bytes())
        print(f"OK max:  {OUT_MAX}")
    except OSError as e:
        print(f"COPY FAILED: {e}")


if __name__ == "__main__":
    build()
