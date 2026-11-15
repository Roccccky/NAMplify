-- NAMplify Auspacken: App um tools/extract-library.sh.
--
-- Doppelklick: packt die Bibliothek unter Dokumente/NAMplify aus.
-- Ordner darauf ziehen: packt genau den aus.
--
-- Das Skript liegt im Bündel (Contents/Resources), die App läuft also für
-- sich allein.

on run
	set src to (POSIX path of (path to documents folder)) & "NAMplify"
	auspacken(src)
end run

on open theItems
	repeat with anItem in theItems
		auspacken(POSIX path of anItem)
	end repeat
end open

on auspacken(src)
	set tool to quoted form of (POSIX path of (path to resource "extract-library.sh"))

	try
		set ergebnis to do shell script "/bin/bash " & tool & " " & quoted form of src
	on error fehler
		display dialog "Das hat nicht geklappt:" & return & return & fehler ¬
			buttons {"OK"} default button 1 with icon stop with title "NAMplify auspacken"
		return
	end try

	set zeilen to paragraphs of ergebnis
	set zusammenfassung to item 1 of zeilen
	set ziel to item ((count of zeilen)) of zeilen

	display dialog zusammenfassung & " ausgepackt." & return & return & ziel ¬
		buttons {"Fertig", "Im Finder zeigen"} default button 2 ¬
		with title "NAMplify auspacken"

	if button returned of result is "Im Finder zeigen" then
		tell application "Finder"
			reveal (POSIX file ziel as alias)
			activate
		end tell
	end if
end auspacken
