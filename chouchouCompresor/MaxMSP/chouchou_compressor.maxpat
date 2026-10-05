{
  "patcher": {
    "fileversion": 1,
    "appversion": {
      "major": 8,
      "minor": 6,
      "revision": 0,
      "architecture": "x64",
      "modernui": 1
    },
    "classnamespace": "box",
    "rect": [
      40,
      40,
      820,
      360
    ],
    "boxes": [
      {
        "box": {
          "id": "title",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            20,
            8,
            700,
            28
          ],
          "text": "chouchouCompressor STEREO | Boost Atk/Rel + GR Atk/Rel separate"
        }
      },
      {
        "box": {
          "id": "in1",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            40,
            45,
            50,
            22
          ],
          "text": "in~ 1",
          "outlettype": [
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "in2",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            100,
            45,
            50,
            22
          ],
          "text": "in~ 2",
          "outlettype": [
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "adc",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            180,
            45,
            50,
            22
          ],
          "text": "adc~",
          "outlettype": [
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "pfft",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 2,
          "patching_rect": [
            40,
            90,
            300,
            22
          ],
          "text": "pfft~ chouchou_compressor_pfft 2048 4",
          "outlettype": [
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "out1",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            40,
            140,
            60,
            22
          ],
          "text": "out~ 1"
        }
      },
      {
        "box": {
          "id": "out2",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            110,
            140,
            60,
            22
          ],
          "text": "out~ 2"
        }
      },
      {
        "box": {
          "id": "dac",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 0,
          "patching_rect": [
            190,
            140,
            50,
            22
          ],
          "text": "dac~"
        }
      },
      {
        "box": {
          "id": "lb",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            700,
            45,
            70,
            22
          ],
          "text": "loadbang",
          "outlettype": [
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "bac",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            320,
            40,
            110,
            18
          ],
          "text": "Boost Atk ms"
        }
      },
      {
        "box": {
          "id": "baf",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            320,
            60,
            50,
            22
          ],
          "text": "float 20",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "ban",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            375,
            60,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "bams",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            320,
            85,
            55,
            22
          ],
          "text": "* 0.001",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "bacl",
          "maxclass": "newobj",
          "numinlets": 3,
          "numoutlets": 1,
          "patching_rect": [
            380,
            85,
            100,
            22
          ],
          "text": "clip 0.000001 10.",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "bae",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            320,
            110,
            160,
            22
          ],
          "text": "expr 1.-exp(-0.01161/$f1)",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "bas",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            490,
            110,
            90,
            22
          ],
          "text": "s boostAtk"
        }
      },
      {
        "box": {
          "id": "brc",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            520,
            40,
            110,
            18
          ],
          "text": "Boost Rel ms"
        }
      },
      {
        "box": {
          "id": "brf",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            520,
            60,
            50,
            22
          ],
          "text": "float 200",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "brn",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            575,
            60,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "brms",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            520,
            85,
            55,
            22
          ],
          "text": "* 0.001",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "brcl",
          "maxclass": "newobj",
          "numinlets": 3,
          "numoutlets": 1,
          "patching_rect": [
            580,
            85,
            100,
            22
          ],
          "text": "clip 0.000001 10.",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "bre",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            520,
            110,
            160,
            22
          ],
          "text": "expr 1.-exp(-0.01161/$f1)",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "brs",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            690,
            110,
            90,
            22
          ],
          "text": "s boostRel"
        }
      },
      {
        "box": {
          "id": "gac",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            320,
            130,
            110,
            18
          ],
          "text": "GR Atk ms"
        }
      },
      {
        "box": {
          "id": "gaf",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            320,
            150,
            50,
            22
          ],
          "text": "float 10",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "gan",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            375,
            150,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "gams",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            320,
            175,
            55,
            22
          ],
          "text": "* 0.001",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "gacl",
          "maxclass": "newobj",
          "numinlets": 3,
          "numoutlets": 1,
          "patching_rect": [
            380,
            175,
            100,
            22
          ],
          "text": "clip 0.000001 10.",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "gae",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            320,
            200,
            160,
            22
          ],
          "text": "expr 1.-exp(-0.01161/$f1)",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "gas",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            490,
            200,
            90,
            22
          ],
          "text": "s grAtk"
        }
      },
      {
        "box": {
          "id": "grc",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            520,
            130,
            110,
            18
          ],
          "text": "GR Rel ms"
        }
      },
      {
        "box": {
          "id": "grf",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            520,
            150,
            50,
            22
          ],
          "text": "float 120",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "grn",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            575,
            150,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "grms",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            520,
            175,
            55,
            22
          ],
          "text": "* 0.001",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "grcl",
          "maxclass": "newobj",
          "numinlets": 3,
          "numoutlets": 1,
          "patching_rect": [
            580,
            175,
            100,
            22
          ],
          "text": "clip 0.000001 10.",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "gre",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            520,
            200,
            160,
            22
          ],
          "text": "expr 1.-exp(-0.01161/$f1)",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "grs",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            690,
            200,
            90,
            22
          ],
          "text": "s grRel"
        }
      },
      {
        "box": {
          "id": "p1c",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            40,
            240,
            100,
            18
          ],
          "text": "Low dB"
        }
      },
      {
        "box": {
          "id": "p1f",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            40,
            262,
            50,
            22
          ],
          "text": "float -50",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "p1n",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            92,
            262,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "p1",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            40,
            290,
            100,
            22
          ],
          "text": "s lowDb"
        }
      },
      {
        "box": {
          "id": "p2c",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            160,
            240,
            100,
            18
          ],
          "text": "High dB"
        }
      },
      {
        "box": {
          "id": "p2f",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            160,
            262,
            50,
            22
          ],
          "text": "float -18",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "p2n",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            212,
            262,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "p2",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            160,
            290,
            100,
            22
          ],
          "text": "s highDb"
        }
      },
      {
        "box": {
          "id": "p3c",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            280,
            240,
            100,
            18
          ],
          "text": "Up Ratio"
        }
      },
      {
        "box": {
          "id": "p3f",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            280,
            262,
            50,
            22
          ],
          "text": "float 2",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "p3n",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            332,
            262,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "p3",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            280,
            290,
            100,
            22
          ],
          "text": "s upRatio"
        }
      },
      {
        "box": {
          "id": "p4c",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            400,
            240,
            100,
            18
          ],
          "text": "Down Ratio"
        }
      },
      {
        "box": {
          "id": "p4f",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            400,
            262,
            50,
            22
          ],
          "text": "float 4",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "p4n",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            452,
            262,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "p4",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            400,
            290,
            100,
            22
          ],
          "text": "s downRatio"
        }
      },
      {
        "box": {
          "id": "p5c",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            520,
            240,
            100,
            18
          ],
          "text": "Mix"
        }
      },
      {
        "box": {
          "id": "p5f",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            520,
            262,
            50,
            22
          ],
          "text": "float 1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "p5n",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            572,
            262,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "p5",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            520,
            290,
            100,
            22
          ],
          "text": "s mix"
        }
      },
      {
        "box": {
          "id": "p6c",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            640,
            240,
            100,
            18
          ],
          "text": "Makeup"
        }
      },
      {
        "box": {
          "id": "p6f",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            640,
            262,
            50,
            22
          ],
          "text": "float 0",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "p6n",
          "maxclass": "flonum",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            692,
            262,
            55,
            22
          ],
          "outlettype": [
            "",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "p6",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            640,
            290,
            100,
            22
          ],
          "text": "s makeupDb"
        }
      }
    ],
    "lines": [
      {
        "patchline": {
          "source": [
            "in1",
            0
          ],
          "destination": [
            "pfft",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "in2",
            0
          ],
          "destination": [
            "pfft",
            1
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "adc",
            0
          ],
          "destination": [
            "pfft",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "adc",
            1
          ],
          "destination": [
            "pfft",
            1
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "pfft",
            0
          ],
          "destination": [
            "out1",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "pfft",
            1
          ],
          "destination": [
            "out2",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "pfft",
            0
          ],
          "destination": [
            "dac",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "pfft",
            1
          ],
          "destination": [
            "dac",
            1
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "baf",
            0
          ],
          "destination": [
            "ban",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "ban",
            0
          ],
          "destination": [
            "bams",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "bams",
            0
          ],
          "destination": [
            "bacl",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "bacl",
            0
          ],
          "destination": [
            "bae",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "bae",
            0
          ],
          "destination": [
            "bas",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "baf",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "brf",
            0
          ],
          "destination": [
            "brn",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "brn",
            0
          ],
          "destination": [
            "brms",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "brms",
            0
          ],
          "destination": [
            "brcl",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "brcl",
            0
          ],
          "destination": [
            "bre",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "bre",
            0
          ],
          "destination": [
            "brs",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "brf",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "gaf",
            0
          ],
          "destination": [
            "gan",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "gan",
            0
          ],
          "destination": [
            "gams",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "gams",
            0
          ],
          "destination": [
            "gacl",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "gacl",
            0
          ],
          "destination": [
            "gae",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "gae",
            0
          ],
          "destination": [
            "gas",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "gaf",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "grf",
            0
          ],
          "destination": [
            "grn",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "grn",
            0
          ],
          "destination": [
            "grms",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "grms",
            0
          ],
          "destination": [
            "grcl",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "grcl",
            0
          ],
          "destination": [
            "gre",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "gre",
            0
          ],
          "destination": [
            "grs",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "grf",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p1f",
            0
          ],
          "destination": [
            "p1n",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p1n",
            0
          ],
          "destination": [
            "p1",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "p1f",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p2f",
            0
          ],
          "destination": [
            "p2n",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p2n",
            0
          ],
          "destination": [
            "p2",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "p2f",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p3f",
            0
          ],
          "destination": [
            "p3n",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p3n",
            0
          ],
          "destination": [
            "p3",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "p3f",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p4f",
            0
          ],
          "destination": [
            "p4n",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p4n",
            0
          ],
          "destination": [
            "p4",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "p4f",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p5f",
            0
          ],
          "destination": [
            "p5n",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p5n",
            0
          ],
          "destination": [
            "p5",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "p5f",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p6f",
            0
          ],
          "destination": [
            "p6n",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "p6n",
            0
          ],
          "destination": [
            "p6",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "lb",
            0
          ],
          "destination": [
            "p6f",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      }
    ]
  }
}