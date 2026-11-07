/* Stats.rexx: Poedit opens a translation and says how far along it is.
   rx Stats.rexx Work:po/de.po   (Poedit running) */
OPTIONS RESULTS
PARSE ARG file
IF ~SHOW('P', 'POEDIT.1') THEN DO
    SAY 'Poedit is not running.'
    EXIT 5
END
ADDRESS 'POEDIT.1'
IF file ~= '' THEN 'OPEN' '"'file'"'
'STATS'
PARSE VAR RESULT all done fuzzy untranslated errors
SAY done 'of' all 'finished,' fuzzy 'fuzzy,' untranslated 'untranslated,' errors 'errors'
