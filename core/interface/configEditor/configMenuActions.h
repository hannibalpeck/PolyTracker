#ifndef INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_
#define INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_

void restartConfigAllActions();
void initConfigValuesAfterReadConfig();

void setRadioRegion();
void setDisplayBrightness();
void setDisplayTheme();
void setDisplayFont();
void refreshThemeAndFontMenuOptions();
void setPadsBrightness();
void setLineInOutType();

void setPadBoardValues();
void setAntialiasingEnable();
void setLimiterMode();
void setSendsMode();
void setHeadroomMode();
void setSnapToZeroMode();

void setMetronomValues();
void setSlaveClockCompValue();

void openCreditsAction();
void openDemosAction();

void startFileTransferMode();


void setFunctionKey1();
void setFunctionKey2();
void setFunctionKey3();
void setFunctionKey4();
void setFunctionKey5();
void setFunctionKey6();
void setFunctionKey7();
void setFunctionKey8();

void resetConfigShowPopup();
void usbStorageEnter();
void usbAudioChange();

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_ */
