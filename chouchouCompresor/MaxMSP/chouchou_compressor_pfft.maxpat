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
      860,
      300
    ],
    "boxes": [
      {
        "box": {
          "id": "Lfftin",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 3,
          "patching_rect": [
            40,
            40,
            80,
            22
          ],
          "text": "fftin~ 1",
          "outlettype": [
            "signal",
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "Lgen",
          "maxclass": "newobj",
          "text": "gen~",
          "numinlets": 3,
          "numoutlets": 2,
          "outlettype": [
            "signal",
            "signal"
          ],
          "patching_rect": [
            40,
            90,
            60,
            22
          ],
          "patcher": {
            "fileversion": 1,
            "appversion": {
              "major": 8,
              "minor": 6,
              "revision": 0,
              "architecture": "x64",
              "modernui": 1
            },
            "rect": [
              34.0,
              34.0,
              700.0,
              520.0
            ],
            "boxes": [
              {
                "box": {
                  "id": "in1",
                  "maxclass": "newobj",
                  "numinlets": 0,
                  "numoutlets": 1,
                  "patching_rect": [
                    21.0,
                    19.0,
                    40.0,
                    22.0
                  ],
                  "text": "in 1",
                  "outlettype": [
                    ""
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
                    120.0,
                    19.0,
                    40.0,
                    22.0
                  ],
                  "text": "in 2",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "id": "in3",
                  "maxclass": "newobj",
                  "numinlets": 0,
                  "numoutlets": 1,
                  "patching_rect": [
                    220.0,
                    19.0,
                    160.0,
                    22.0
                  ],
                  "text": "in 3 @comment \"bin\"",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "code": "Param boostAtk(0.3);\nParam boostRel(0.08);\nParam grAtk(0.4);\nParam grRel(0.1);\nParam lowDb(-50);\nParam highDb(-18);\nParam upRatio(2);\nParam downRatio(4);\nParam mix(1);\nParam makeupDb(0);\nHistory boostG(1);\nHistory grG(1);\n\n// Match JUCE: UI lowDb/highDb are dBFS. Max fftin~ @2048 raw amp ≈ N/4 = 512 for FS sine.\nspectralFS = 512;\nnoiseFloorDb = -90;\namp, phase = cartopol(in1, in2);\nmag = max(amp, 1e-8);\nlowLin = pow(10, lowDb * 0.05) * spectralFS;\nhighLin = pow(10, highDb * 0.05) * spectralFS;\nnoiseLin = pow(10, noiseFloorDb * 0.05) * spectralFS;\nmagDbFs = 20 * log10(max(mag / spectralFS, 1e-8));\nupSlope = 1 - 1 / max(upRatio, 1);\ndnSlope = 1 - 1 / max(downRatio, 1);\n// Boost only between noise floor and low thresh (never expand empty bins).\nboostTarget = (mag >= noiseLin && mag < lowLin) ? pow(10, ((lowDb - magDbFs) * upSlope) * 0.05) : 1;\ngrTarget = (mag > highLin) ? pow(10, ((highDb - magDbFs) * dnSlope) * 0.05) : 1;\nboostTarget = clip(boostTarget, 1, 32);\ngrTarget = clip(grTarget, 1e-4, 1);\nbCoeff = (boostTarget > boostG) ? boostAtk : boostRel;\nboostG = boostG + bCoeff * (boostTarget - boostG);\ngCoeff = (grTarget < grG) ? grAtk : grRel;\ngrG = grG + gCoeff * (grTarget - grG);\ngain = (1 - mix) + mix * (boostG * grG * pow(10, makeupDb * 0.05));\nout1, out2 = poltocar(amp * gain, phase);\n",
                  "fontface": 0,
                  "fontname": "Lato",
                  "fontsize": 12.0,
                  "id": "code",
                  "maxclass": "codebox",
                  "numinlets": 3,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    ""
                  ],
                  "patching_rect": [
                    21.0,
                    55.0,
                    640.0,
                    400.0
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
                    21.0,
                    470.0,
                    48.0,
                    22.0
                  ],
                  "text": "out 1"
                }
              },
              {
                "box": {
                  "id": "out2",
                  "maxclass": "newobj",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "patching_rect": [
                    120.0,
                    470.0,
                    48.0,
                    22.0
                  ],
                  "text": "out 2"
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
                    "code",
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
                    "code",
                    1
                  ],
                  "disabled": 0,
                  "hidden": 0
                }
              },
              {
                "patchline": {
                  "source": [
                    "in3",
                    0
                  ],
                  "destination": [
                    "code",
                    2
                  ],
                  "disabled": 0,
                  "hidden": 0
                }
              },
              {
                "patchline": {
                  "source": [
                    "code",
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
                    "code",
                    1
                  ],
                  "destination": [
                    "out2",
                    0
                  ],
                  "disabled": 0,
                  "hidden": 0
                }
              }
            ]
          }
        }
      },
      {
        "box": {
          "id": "Lfftout",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 0,
          "patching_rect": [
            40,
            140,
            80,
            22
          ],
          "text": "fftout~ 1"
        }
      },
      {
        "box": {
          "id": "Rfftin",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 3,
          "patching_rect": [
            280,
            40,
            80,
            22
          ],
          "text": "fftin~ 2",
          "outlettype": [
            "signal",
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "Rgen",
          "maxclass": "newobj",
          "text": "gen~",
          "numinlets": 3,
          "numoutlets": 2,
          "outlettype": [
            "signal",
            "signal"
          ],
          "patching_rect": [
            280,
            90,
            60,
            22
          ],
          "patcher": {
            "fileversion": 1,
            "appversion": {
              "major": 8,
              "minor": 6,
              "revision": 0,
              "architecture": "x64",
              "modernui": 1
            },
            "rect": [
              34.0,
              34.0,
              700.0,
              520.0
            ],
            "boxes": [
              {
                "box": {
                  "id": "in1",
                  "maxclass": "newobj",
                  "numinlets": 0,
                  "numoutlets": 1,
                  "patching_rect": [
                    21.0,
                    19.0,
                    40.0,
                    22.0
                  ],
                  "text": "in 1",
                  "outlettype": [
                    ""
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
                    120.0,
                    19.0,
                    40.0,
                    22.0
                  ],
                  "text": "in 2",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "id": "in3",
                  "maxclass": "newobj",
                  "numinlets": 0,
                  "numoutlets": 1,
                  "patching_rect": [
                    220.0,
                    19.0,
                    160.0,
                    22.0
                  ],
                  "text": "in 3 @comment \"bin\"",
                  "outlettype": [
                    ""
                  ]
                }
              },
              {
                "box": {
                  "code": "Param boostAtk(0.3);\nParam boostRel(0.08);\nParam grAtk(0.4);\nParam grRel(0.1);\nParam lowDb(-50);\nParam highDb(-18);\nParam upRatio(2);\nParam downRatio(4);\nParam mix(1);\nParam makeupDb(0);\nHistory boostG(1);\nHistory grG(1);\n\n// Match JUCE: UI lowDb/highDb are dBFS. Max fftin~ @2048 raw amp ≈ N/4 = 512 for FS sine.\nspectralFS = 512;\nnoiseFloorDb = -90;\namp, phase = cartopol(in1, in2);\nmag = max(amp, 1e-8);\nlowLin = pow(10, lowDb * 0.05) * spectralFS;\nhighLin = pow(10, highDb * 0.05) * spectralFS;\nnoiseLin = pow(10, noiseFloorDb * 0.05) * spectralFS;\nmagDbFs = 20 * log10(max(mag / spectralFS, 1e-8));\nupSlope = 1 - 1 / max(upRatio, 1);\ndnSlope = 1 - 1 / max(downRatio, 1);\n// Boost only between noise floor and low thresh (never expand empty bins).\nboostTarget = (mag >= noiseLin && mag < lowLin) ? pow(10, ((lowDb - magDbFs) * upSlope) * 0.05) : 1;\ngrTarget = (mag > highLin) ? pow(10, ((highDb - magDbFs) * dnSlope) * 0.05) : 1;\nboostTarget = clip(boostTarget, 1, 32);\ngrTarget = clip(grTarget, 1e-4, 1);\nbCoeff = (boostTarget > boostG) ? boostAtk : boostRel;\nboostG = boostG + bCoeff * (boostTarget - boostG);\ngCoeff = (grTarget < grG) ? grAtk : grRel;\ngrG = grG + gCoeff * (grTarget - grG);\ngain = (1 - mix) + mix * (boostG * grG * pow(10, makeupDb * 0.05));\nout1, out2 = poltocar(amp * gain, phase);\n",
                  "fontface": 0,
                  "fontname": "Lato",
                  "fontsize": 12.0,
                  "id": "code",
                  "maxclass": "codebox",
                  "numinlets": 3,
                  "numoutlets": 2,
                  "outlettype": [
                    "",
                    ""
                  ],
                  "patching_rect": [
                    21.0,
                    55.0,
                    640.0,
                    400.0
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
                    21.0,
                    470.0,
                    48.0,
                    22.0
                  ],
                  "text": "out 1"
                }
              },
              {
                "box": {
                  "id": "out2",
                  "maxclass": "newobj",
                  "numinlets": 1,
                  "numoutlets": 0,
                  "patching_rect": [
                    120.0,
                    470.0,
                    48.0,
                    22.0
                  ],
                  "text": "out 2"
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
                    "code",
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
                    "code",
                    1
                  ],
                  "disabled": 0,
                  "hidden": 0
                }
              },
              {
                "patchline": {
                  "source": [
                    "in3",
                    0
                  ],
                  "destination": [
                    "code",
                    2
                  ],
                  "disabled": 0,
                  "hidden": 0
                }
              },
              {
                "patchline": {
                  "source": [
                    "code",
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
                    "code",
                    1
                  ],
                  "destination": [
                    "out2",
                    0
                  ],
                  "disabled": 0,
                  "hidden": 0
                }
              }
            ]
          }
        }
      },
      {
        "box": {
          "id": "Rfftout",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 0,
          "patching_rect": [
            280,
            140,
            80,
            22
          ],
          "text": "fftout~ 2"
        }
      },
      {
        "box": {
          "id": "r_boostAtk",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            500,
            110,
            90,
            22
          ],
          "text": "r boostAtk",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_boostAtk",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            595,
            110,
            110,
            22
          ],
          "text": "boostAtk $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_boostRel",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            500,
            140,
            90,
            22
          ],
          "text": "r boostRel",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_boostRel",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            595,
            140,
            110,
            22
          ],
          "text": "boostRel $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_grAtk",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            500,
            170,
            90,
            22
          ],
          "text": "r grAtk",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_grAtk",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            595,
            170,
            110,
            22
          ],
          "text": "grAtk $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_grRel",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            500,
            200,
            90,
            22
          ],
          "text": "r grRel",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_grRel",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            595,
            200,
            110,
            22
          ],
          "text": "grRel $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_lowDb",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            620,
            110,
            90,
            22
          ],
          "text": "r lowDb",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_lowDb",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            715,
            110,
            110,
            22
          ],
          "text": "lowDb $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_highDb",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            620,
            140,
            90,
            22
          ],
          "text": "r highDb",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_highDb",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            715,
            140,
            110,
            22
          ],
          "text": "highDb $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_upRatio",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            620,
            170,
            90,
            22
          ],
          "text": "r upRatio",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_upRatio",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            715,
            170,
            110,
            22
          ],
          "text": "upRatio $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_downRatio",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            620,
            200,
            90,
            22
          ],
          "text": "r downRatio",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_downRatio",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            715,
            200,
            110,
            22
          ],
          "text": "downRatio $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_mix",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            500,
            230,
            90,
            22
          ],
          "text": "r mix",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_mix",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            595,
            230,
            110,
            22
          ],
          "text": "mix $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "r_makeupDb",
          "maxclass": "newobj",
          "numinlets": 0,
          "numoutlets": 1,
          "patching_rect": [
            620,
            230,
            90,
            22
          ],
          "text": "r makeupDb",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "m_makeupDb",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            715,
            230,
            110,
            22
          ],
          "text": "makeupDb $1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "cmt",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            500,
            40,
            300,
            50
          ],
          "text": "STEREO + separate Boost/GR Attack Release"
        }
      }
    ],
    "lines": [
      {
        "patchline": {
          "source": [
            "Lfftin",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Lfftin",
            1
          ],
          "destination": [
            "Lgen",
            1
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Lfftin",
            2
          ],
          "destination": [
            "Lgen",
            2
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Lgen",
            0
          ],
          "destination": [
            "Lfftout",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Lgen",
            1
          ],
          "destination": [
            "Lfftout",
            1
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Rfftin",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Rfftin",
            1
          ],
          "destination": [
            "Rgen",
            1
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Rfftin",
            2
          ],
          "destination": [
            "Rgen",
            2
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Rgen",
            0
          ],
          "destination": [
            "Rfftout",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "Rgen",
            1
          ],
          "destination": [
            "Rfftout",
            1
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_boostAtk",
            0
          ],
          "destination": [
            "m_boostAtk",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_boostAtk",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_boostAtk",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_boostRel",
            0
          ],
          "destination": [
            "m_boostRel",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_boostRel",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_boostRel",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_grAtk",
            0
          ],
          "destination": [
            "m_grAtk",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_grAtk",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_grAtk",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_grRel",
            0
          ],
          "destination": [
            "m_grRel",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_grRel",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_grRel",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_lowDb",
            0
          ],
          "destination": [
            "m_lowDb",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_lowDb",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_lowDb",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_highDb",
            0
          ],
          "destination": [
            "m_highDb",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_highDb",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_highDb",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_upRatio",
            0
          ],
          "destination": [
            "m_upRatio",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_upRatio",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_upRatio",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_downRatio",
            0
          ],
          "destination": [
            "m_downRatio",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_downRatio",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_downRatio",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_mix",
            0
          ],
          "destination": [
            "m_mix",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_mix",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_mix",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "r_makeupDb",
            0
          ],
          "destination": [
            "m_makeupDb",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_makeupDb",
            0
          ],
          "destination": [
            "Lgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      },
      {
        "patchline": {
          "source": [
            "m_makeupDb",
            0
          ],
          "destination": [
            "Rgen",
            0
          ],
          "disabled": 0,
          "hidden": 0
        }
      }
    ]
  }
}
