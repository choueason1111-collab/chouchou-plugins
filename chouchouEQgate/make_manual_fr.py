#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Génère le manuel PDF français de chouchouEQGate."""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm, mm
from reportlab.platypus import (
    KeepTogether,
    ListFlowable,
    ListItem,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
    HRFlowable,
)

OUT = Path(__file__).resolve().parent / "chouchouEQGate_Manuel_FR.pdf"

# Palette
INK = colors.HexColor("#1a1a1a")
MUTED = colors.HexColor("#555555")
ACCENT = colors.HexColor("#2c5f4a")
RULE = colors.HexColor("#c8c8c8")
SOFT = colors.HexColor("#f4f6f5")
HEADER_BG = colors.HexColor("#2c5f4a")


def styles():
    base = getSampleStyleSheet()
    s = {
        "cover_brand": ParagraphStyle(
            "cover_brand",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=28,
            textColor=ACCENT,
            alignment=TA_CENTER,
            spaceAfter=8,
            leading=34,
        ),
        "cover_title": ParagraphStyle(
            "cover_title",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=16,
            textColor=INK,
            alignment=TA_CENTER,
            spaceAfter=6,
            leading=22,
        ),
        "cover_sub": ParagraphStyle(
            "cover_sub",
            parent=base["Normal"],
            fontName="Helvetica-Oblique",
            fontSize=11,
            textColor=MUTED,
            alignment=TA_CENTER,
            spaceAfter=4,
            leading=15,
        ),
        "h1": ParagraphStyle(
            "h1",
            parent=base["Heading1"],
            fontName="Helvetica-Bold",
            fontSize=14,
            textColor=ACCENT,
            spaceBefore=16,
            spaceAfter=8,
            leading=18,
        ),
        "h2": ParagraphStyle(
            "h2",
            parent=base["Heading2"],
            fontName="Helvetica-Bold",
            fontSize=11.5,
            textColor=INK,
            spaceBefore=12,
            spaceAfter=6,
            leading=15,
        ),
        "body": ParagraphStyle(
            "body",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=9.5,
            textColor=INK,
            alignment=TA_JUSTIFY,
            spaceAfter=7,
            leading=13.5,
        ),
        "bullet": ParagraphStyle(
            "bullet",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=9.5,
            textColor=INK,
            leftIndent=4,
            spaceAfter=3,
            leading=13,
        ),
        "caption": ParagraphStyle(
            "caption",
            parent=base["Normal"],
            fontName="Helvetica-Oblique",
            fontSize=8.5,
            textColor=MUTED,
            alignment=TA_CENTER,
            spaceBefore=4,
            spaceAfter=10,
            leading=11,
        ),
        "formula": ParagraphStyle(
            "formula",
            parent=base["Normal"],
            fontName="Courier",
            fontSize=10,
            textColor=INK,
            alignment=TA_CENTER,
            spaceBefore=8,
            spaceAfter=8,
            leading=14,
            backColor=SOFT,
        ),
        "footer": ParagraphStyle(
            "footer",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=8,
            textColor=MUTED,
            alignment=TA_CENTER,
        ),
        "th": ParagraphStyle(
            "th",
            parent=base["Normal"],
            fontName="Helvetica-Bold",
            fontSize=8.5,
            textColor=colors.white,
            leading=11,
        ),
        "td": ParagraphStyle(
            "td",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=8.5,
            textColor=INK,
            leading=11,
        ),
        "note": ParagraphStyle(
            "note",
            parent=base["Normal"],
            fontName="Helvetica",
            fontSize=9,
            textColor=INK,
            alignment=TA_LEFT,
            spaceAfter=8,
            leading=12.5,
            leftIndent=8,
            rightIndent=8,
            borderPadding=6,
        ),
    }
    return s


def add_page_number(canvas, doc):
    canvas.saveState()
    page = canvas.getPageNumber()
    if page > 1:
        canvas.setStrokeColor(RULE)
        canvas.setLineWidth(0.4)
        w, h = A4
        canvas.line(2 * cm, 1.4 * cm, w - 2 * cm, 1.4 * cm)
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(MUTED)
        canvas.drawCentredString(w / 2, 0.9 * cm, f"chouchouEQGate — Manuel · {page}")
        canvas.drawString(2 * cm, 0.9 * cm, "chouchou")
        canvas.drawRightString(w - 2 * cm, 0.9 * cm, "FR")
    canvas.restoreState()


def bullet_list(items, st):
    return ListFlowable(
        [ListItem(Paragraph(t, st["bullet"]), leftIndent=12, bulletColor=ACCENT) for t in items],
        bulletType="bullet",
        start="•",
        leftIndent=18,
        bulletFontSize=9,
        spaceBefore=2,
        spaceAfter=8,
    )


def param_table(rows, st):
    data = [[Paragraph(c, st["th"]) for c in ["Paramètre", "Rôle", "Conseils"]]]
    for r in rows:
        data.append([Paragraph(x, st["td"]) for x in r])
    t = Table(data, colWidths=[3.2 * cm, 6.5 * cm, 6.3 * cm])
    t.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), HEADER_BG),
                ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
                ("BACKGROUND", (0, 1), (-1, -1), colors.white),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, SOFT]),
                ("GRID", (0, 0), (-1, -1), 0.4, RULE),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("RIGHTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ]
        )
    )
    return t


def build():
    st = styles()
    doc = SimpleDocTemplate(
        str(OUT),
        pagesize=A4,
        leftMargin=2 * cm,
        rightMargin=2 * cm,
        topMargin=1.8 * cm,
        bottomMargin=2 * cm,
        title="chouchouEQGate — Manuel d'utilisation",
        author="chouchou",
        subject="Guide détaillé du processeur spectral Expand / Gate",
        creator="chouchouEQGate",
    )

    story = []

    # —— Couverture ——
    story.append(Spacer(1, 3.5 * cm))
    story.append(Paragraph("chouchou", st["cover_brand"]))
    story.append(HRFlowable(width="40%", thickness=1.2, color=ACCENT, spaceBefore=4, spaceAfter=14, hAlign="CENTER"))
    story.append(Paragraph("chouchouEQGate", st["cover_title"]))
    story.append(
        Paragraph(
            "Manuel d'utilisation — Expand / Gate spectral",
            st["cover_sub"],
        )
    )
    story.append(Spacer(1, 0.6 * cm))
    story.append(
        Paragraph(
            "Processeur FFT qui renforce les fréquences fortes<br/>et affaiblit les fréquences faibles, bande par bande.",
            st["cover_sub"],
        )
    )
    story.append(Spacer(1, 2.2 * cm))
    story.append(Paragraph("Versions : VST3 / AU (JUCE) · Max for Live / MaxMSP", st["cover_sub"]))
    story.append(Paragraph("Langue : français · 2026", st["cover_sub"]))
    story.append(PageBreak())

    # —— 1. Qu'est-ce que c'est ——
    story.append(Paragraph("1. Qu'est-ce que chouchouEQGate ?", st["h1"]))
    story.append(
        Paragraph(
            "chouchouEQGate est un <b>expanseur / gate spectral</b>. Contrairement à un gate classique "
            "qui écoute le niveau global du signal, il analyse le son dans le domaine fréquentiel "
            "(FFT) et applique un gain <b>indépendant à chaque bande de fréquence</b> (chaque « bin »).",
            st["body"],
        )
    )
    story.append(
        Paragraph(
            "L'idée est simple : les composantes déjà fortes deviennent encore plus présentes ; "
            "les composantes faibles sont réduites ou coupées. Le résultat peut clarifier un mix, "
            "accentuer des attaques, nettoyer un bruit de fond, ou créer des textures très "
            "contrastées — selon le mode et les réglages.",
            st["body"],
        )
    )
    story.append(Paragraph("En une phrase", st["h2"]))
    story.append(
        Paragraph(
            "<i>Ce qui est fort devient plus fort ; ce qui est faible devient plus faible — "
            "fréquence par fréquence.</i>",
            st["body"],
        )
    )

    # —— 2. Différence gate classique ——
    story.append(Paragraph("2. Différence avec un gate classique", st["h1"]))
    story.append(
        Paragraph(
            "Un gate (ou noise gate) traditionnel mesure souvent l'énergie globale (ou d'une bande "
            "large) et ouvre / ferme le chemin audio d'un coup. Ici, chaque bin FFT a son propre "
            "seuil et sa propre enveloppe. Une caisse claire peut rester forte pendant qu'un "
            "souffle aigu est atténué, sans « fermer » tout le signal.",
            st["body"],
        )
    )

    cmp_data = [
        [Paragraph(c, st["th"]) for c in ["Aspect", "Gate classique", "chouchouEQGate"]],
        [
            Paragraph("Mesure", st["td"]),
            Paragraph("Niveau global (souvent)", st["td"]),
            Paragraph("Magnitude par bin FFT", st["td"]),
        ],
        [
            Paragraph("Action", st["td"]),
            Paragraph("Ouvre / ferme le signal", st["td"]),
            Paragraph("Gain local par fréquence", st["td"]),
        ],
        [
            Paragraph("Attack / Release", st["td"]),
            Paragraph("Temps sur le niveau global", st["td"]),
            Paragraph("Lissage par bande (enveloppe spectrale)", st["td"]),
        ],
        [
            Paragraph("Ratio", st["td"]),
            Paragraph("Souvent en dB (linéaire en log)", st["td"]),
            Paragraph("Exposant (Amount) — non linéaire", st["td"]),
        ],
    ]
    cmp = Table(cmp_data, colWidths=[3.2 * cm, 6.4 * cm, 6.4 * cm])
    cmp.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), HEADER_BG),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, SOFT]),
                ("GRID", (0, 0), (-1, -1), 0.4, RULE),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
            ]
        )
    )
    story.append(cmp)
    story.append(Paragraph("Tableau comparatif — gate large bande vs traitement spectral", st["caption"]))

    # —— 3. Modes ——
    story.append(Paragraph("3. Les deux modes (plugin JUCE)", st["h1"]))
    story.append(Paragraph("Mode Expand", st["h2"]))
    story.append(
        Paragraph(
            "Au-dessus du seuil, le gain peut monter au-delà de 1 (les pics sont accentués). "
            "En dessous, le gain descend sous 1 (les vallées sont creusées). C'est l'effet le plus "
            "« expansif » et spectaculaire.",
            st["body"],
        )
    )
    story.append(Paragraph("Mode Gate", st["h2"]))
    story.append(
        Paragraph(
            "Seule la partie <b>sous</b> le seuil est affaiblie : le gain est plafonné à 1 "
            "(<font face='Courier'>min(gain, 1)</font>). Les fréquences déjà fortes ne sont pas "
            "boostées ; on nettoie plutôt le fond. La version Max for Live actuelle fonctionne "
            "essentiellement en logique Gate.",
            st["body"],
        )
    )

    # —— 4. Formule ——
    story.append(Paragraph("4. Principe de calcul", st["h1"]))
    story.append(
        Paragraph(
            "Pour chaque bin, on calcule un gain à partir du rapport entre la magnitude et le "
            "seuil, élevé à la puissance <b>Amount</b> :",
            st["body"],
        )
    )
    story.append(Paragraph("gain = (magnitude / seuil) ^ amount", st["formula"]))
    story.append(
        Paragraph(
            "Puis ce gain est lissé dans le temps (Attack / Release) avant d'être appliqué à "
            "l'amplitude, pendant que la phase est conservée (reconstruction polaire → cartésienne). "
            "En mode Gate, le gain final est limité à 1.",
            st["body"],
        )
    )
    story.append(
        Paragraph(
            "<b>Important :</b> Amount n'est pas un « ratio » classique en dB. C'est un "
            "<b>exposant</b> : plus il est élevé, plus la courbe est agressive. Ce n'est pas "
            "linéaire comme un compresseur / expanseur studio classique.",
            st["body"],
        )
    )

    # —— 5. Chaîne DSP ——
    story.append(Paragraph("5. Chaîne de traitement (aperçu)", st["h1"]))
    story.append(bullet_list(
        [
            "<b>STFT</b> : FFT 2048 points, hop 512 (chevauchement 75 %), fenêtre de Hann, "
            "recouvrement-addition (OLA).",
            "<b>Cartésien → polaire</b> : magnitude et phase par bin.",
            "<b>Enveloppe</b> : lissage Attack / Release sur la magnitude (ou le gain).",
            "<b>Gain spectral</b> : formule ci-dessus, puis clip éventuel en mode Gate.",
            "<b>Polaire → cartésien → IFFT</b> : reconstruction du signal temporel.",
        ],
        st,
    ))
    story.append(
        Paragraph(
            "En MaxMSP / Max for Live, le cœur tourne dans <font face='Courier'>pfft~</font> "
            "(sous-patch <font face='Courier'>fftin</font>) avec "
            "<font face='Courier'>cartopol~</font>, <font face='Courier'>rampsmooth~</font>, "
            "<font face='Courier'>/~</font>, <font face='Courier'>pow~</font>, "
            "<font face='Courier'>clip~</font>, <font face='Courier'>poltocar~</font>.",
            st["body"],
        )
    )

    # —— 6. Paramètres ——
    story.append(Paragraph("6. Paramètres détaillés", st["h1"]))
    story.append(
        Paragraph(
            "Les noms varient légèrement entre le plugin JUCE (boutons) et Max for Live "
            "(cases numériques), mais le sens est le même.",
            st["body"],
        )
    )
    story.append(
        param_table(
            [
                [
                    "<b>Threshold</b><br/>(seuil)",
                    "Niveau de référence par bin. Au-dessus : moins d'atténuation (voire boost en Expand). "
                    "En dessous : réduction selon Amount.",
                    "Baisser pour « manger » plus de fond. Monter pour ne toucher que les très faibles "
                    "composantes. Commencer bas et remonter à l'oreille.",
                ],
                [
                    "<b>Amount</b>",
                    "Exposant de la courbe. Contrôle la dureté du contraste spectral.",
                    "Valeurs faibles = subtil. Valeurs élevées = très sélectif / agressif. "
                    "Ce n'est pas un ratio dB classique.",
                ],
                [
                    "<b>Attack</b>",
                    "Vitesse à laquelle le gain suit une montée d'énergie dans un bin.",
                    "Trop rapide : artefacts / « chatter ». Un peu plus long = plus musical.",
                ],
                [
                    "<b>Release</b>",
                    "Vitesse de retour quand l'énergie d'un bin redescend.",
                    "Court = nettoyage sec. Long = queue plus douce, moins de pompage spectral.",
                ],
                [
                    "<b>Mix</b><br/>(JUCE)",
                    "Mélange dry / wet.",
                    "Utile pour garder le corps du son tout en ajoutant le contraste.",
                ],
                [
                    "<b>Makeup</b><br/>(JUCE)",
                    "Gain de sortie pour compenser le niveau après traitement.",
                    "Après un Gate fort, remonter légèrement le makeup.",
                ],
                [
                    "<b>Mode</b><br/>(JUCE)",
                    "Expand ou Gate.",
                    "Expand pour accentuer ; Gate pour nettoyer sans booster les pics.",
                ],
            ],
            st,
        )
    )
    story.append(Spacer(1, 0.3 * cm))

    # —— 7. Installation ——
    story.append(Paragraph("7. Installation", st["h1"]))
    story.append(Paragraph("Plugin natif (VST3 / AU)", st["h2"]))
    story.append(
        Paragraph(
            "Le plugin <b>chouchouEQGate</b> (fabricant <b>chouchou</b>) est fourni en "
            "binaire universel macOS (x86_64 + arm64) pour Intel et Apple Silicon.",
            st["body"],
        )
    )
    story.append(bullet_list(
        [
            "<b>VST3</b> : copier <font face='Courier'>chouchouEQGate.vst3</font> dans "
            "<font face='Courier'>~/Library/Audio/Plug-Ins/VST3/</font>",
            "<b>AU</b> : copier le composant Audio Unit dans "
            "<font face='Courier'>~/Library/Audio/Plug-Ins/Components/</font>",
            "Rescanner les plugins dans le DAW (Ableton, Logic, Reaper, etc.).",
            "Un pack de partage peut aussi contenir <font face='Courier'>chouchouFFT1</font> "
            "et <font face='Courier'>AIFFT1</font>.",
        ],
        st,
    ))
    story.append(Paragraph("Max for Live / MaxMSP", st["h2"]))
    story.append(bullet_list(
        [
            "Placer <font face='Courier'>chouchouEQGate.amxd</font> (ou le patch parent) "
            "<b>dans le même dossier</b> que <font face='Courier'>fftin.maxpat</font>.",
            "Pour Live : dossier User Library → Presets → Audio Effects → Max Audio Effect.",
            "Sans <font face='Courier'>fftin.maxpat</font> à côté du device, "
            "<font face='Courier'>pfft~</font> ne trouvera pas le sous-patch.",
            "Dans MaxMSP : ouvrir le patch parent ; audio via "
            "<font face='Courier'>plugin~</font> / <font face='Courier'>plugout~</font> "
            "ou entrées/sorties audio selon votre patch.",
        ],
        st,
    ))

    # —— 8. Utilisation pratique ——
    story.append(Paragraph("8. Utilisation pratique", st["h1"]))
    story.append(Paragraph("Par où commencer", st["h2"]))
    story.append(bullet_list(
        [
            "Mode <b>Gate</b>, Mix à 100 %, Makeup neutre.",
            "Threshold bas, Amount modéré — écouter le fond qui disparaît.",
            "Monter le Threshold jusqu'à ce que le corps du son reste intact.",
            "Régler Attack / Release pour éviter les artefacts et le pompage.",
            "Passer en Expand seulement si vous voulez vraiment accentuer les pics.",
            "Baisser le Mix (ex. 30–60 %) pour un effet plus transparent.",
        ],
        st,
    ))
    story.append(Paragraph("Cas d'usage", st["h2"]))
    story.append(bullet_list(
        [
            "<b>Voix / parole</b> : réduire souffle et réverb parasite entre les phrases "
            "(Gate doux).",
            "<b>Batterie / percussions</b> : faire ressortir les attaques (Expand prudent).",
            "<b>Pads / ambient</b> : sculpter le spectre, créer des trous « vivants ».",
            "<b>Nettoyage</b> : atténuer un bruit de fond large bande sans EQ manuel.",
            "<b>Créatif</b> : Amount très élevé + Release court pour des textures hachées.",
        ],
        st,
    ))
    story.append(Paragraph("Pièges à éviter", st["h2"]))
    story.append(bullet_list(
        [
            "Threshold trop bas + Amount trop fort → son « creux », phasey, fatigué.",
            "Attack / Release trop courts → artefacts FFT, scintillement.",
            "Comparer avec un gate large bande : le feeling des ms n'est pas le même.",
            "Sur un bus déjà compressé, l'effet peut paraître excessif — commencer doux.",
        ],
        st,
    ))

    # —— 9. Contrôles Max ——
    story.append(Paragraph("9. Correspondance des entrées (Max / M4L)", st["h1"]))
    story.append(
        Paragraph(
            "Dans le sous-patch FFT, les paramètres arrivent sur des inlets dédiés "
            "(éviter les conflits avec les canaux audio de <font face='Courier'>fftin~</font>) :",
            st["body"],
        )
    )
    m4l = [
        [Paragraph(c, st["th"]) for c in ["Inlet", "Paramètre"]],
        [Paragraph("1–2", st["td"]), Paragraph("Audio L / R", st["td"])],
        [Paragraph("3", st["td"]), Paragraph("Threshold (seuil)", st["td"])],
        [Paragraph("4", st["td"]), Paragraph("Amount", st["td"])],
        [Paragraph("5", st["td"]), Paragraph("Attack", st["td"])],
        [Paragraph("6", st["td"]), Paragraph("Release", st["td"])],
    ]
    mt = Table(m4l, colWidths=[3 * cm, 13 * cm])
    mt.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), HEADER_BG),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, SOFT]),
                ("GRID", (0, 0), (-1, -1), 0.4, RULE),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
            ]
        )
    )
    story.append(mt)
    story.append(Spacer(1, 0.4 * cm))
    story.append(
        Paragraph(
            "Les messages de type liste doivent être séparés par des espaces "
            "(pas de virgules). Un objet <font face='Courier'>route all</font> "
            "peut précéder les destinations de paramètres pour éviter les erreurs "
            "« bad number » liées au message <font face='Courier'>all</font> de Live.",
            st["body"],
        )
    )

    # —— 10. Specs ——
    story.append(Paragraph("10. Spécifications techniques", st["h1"]))
    specs = [
        [Paragraph(c, st["th"]) for c in ["Élément", "Valeur"]],
        [Paragraph("Nom du plugin", st["td"]), Paragraph("chouchouEQGate", st["td"])],
        [Paragraph("Fabricant", st["td"]), Paragraph("chouchou", st["td"])],
        [Paragraph("Formats", st["td"]), Paragraph("VST3, AU ; Max for Live / MaxMSP", st["td"])],
        [Paragraph("Architecture Mac", st["td"]), Paragraph("Universelle (Intel + Apple Silicon)", st["td"])],
        [Paragraph("Taille FFT", st["td"]), Paragraph("2048", st["td"])],
        [Paragraph("Hop / chevauchement", st["td"]), Paragraph("512 · 75 %", st["td"])],
        [Paragraph("Fenêtre", st["td"]), Paragraph("Hann + overlap-add", st["td"])],
        [Paragraph("Canaux", st["td"]), Paragraph("Stéréo (traitement spectral L/R)", st["td"])],
    ]
    sp = Table(specs, colWidths=[5 * cm, 11 * cm])
    sp.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), HEADER_BG),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, SOFT]),
                ("GRID", (0, 0), (-1, -1), 0.4, RULE),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
            ]
        )
    )
    story.append(sp)

    # —— 11. Glossaire ——
    story.append(Paragraph("11. Petit glossaire", st["h1"]))
    story.append(bullet_list(
        [
            "<b>Bin</b> : une « case » de fréquence dans la FFT.",
            "<b>STFT</b> : transformée de Fourier à court terme (analyse fenêtre par fenêtre).",
            "<b>Magnitude</b> : amplitude d'un bin (force de cette fréquence).",
            "<b>Phase</b> : position temporelle de l'onde ; ici elle est préservée.",
            "<b>Expanseur</b> : augmente le contraste dynamique (inverse d'un compresseur).",
            "<b>Gate</b> : coupe ou réduit ce qui est sous un seuil.",
        ],
        st,
    ))

    story.append(Spacer(1, 0.8 * cm))
    story.append(HRFlowable(width="100%", thickness=0.6, color=RULE, spaceBefore=4, spaceAfter=10))
    story.append(
        Paragraph(
            "Document rédigé pour chouchouEQGate · Fabricant chouchou · Usage pédagogique et créatif.<br/>"
            "Pour toute question d'installation, vérifier que le fichier FFT compagnon "
            "(fftin.maxpat) accompagne bien le device Max for Live.",
            st["caption"],
        )
    )

    doc.build(story, onFirstPage=add_page_number, onLaterPages=add_page_number)
    print(f"OK: {OUT}")


if __name__ == "__main__":
    build()
