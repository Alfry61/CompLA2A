# Regole del progetto

- Framework: JUCE 8, formato VST3, C++17.
- Nel processBlock: nessuna allocazione di memoria, nessun lock,
  nessuna chiamata di sistema, nessun log.
- Parametri solo tramite AudioProcessorValueTreeState; leggerli con
  getRawParameterValue e usare SmoothedValue per evitare scatti.
- Gestire i denormali con juce::ScopedNoDenormals.
- Tutte le variabili di stato DSP vanno reimpostate in prepareToPlay.
- Non modificare i file generati da Projucer senza chiedere.
- Dopo ogni modifica proponi il comando di compilazione, non lanciarlo
  senza conferma.