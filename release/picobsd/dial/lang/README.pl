1998.07.23, Warszawa

			PicoBSD @VER@ (wersja DIALUP)
			---------------------------

Co to jest PicoBSD?
-------------------

Jest to jednodyskietkowa wersja FreeBSD skonfigurowana g��wnie pod k�tem
zastosowania jako narz�dzie dost�pu przez dialup lub ethernet.
W celu zapoznania si� z pe�nym systemem zajrzyj na http://www.freebsd.org

Jakie s� minimalne wymagania?
-----------------------------

* Procesor 386SX lub lepszy (dost�pny jest emulator FPU)
* 8MB pami�ci - jest to absolutnie nieprzekraczalne minimum. Oczywi�cie im
  wiecej, tym lepiej - ograniczenie jest g��wnie spowodowane brakiem swapu. Po
  zapoznaniu si� z systemem mo�esz sobie skonfigurowa� tzw. swap-file na dysku
  twardym, np. na partycji DOS-owej lub Linux-owej. S�u�y do tego program
  vnconfig, oraz urz�dzenie vn(4). W�wczas prawdopodobnie wystarczy 4MB pami�ci.
* Modem, skonfigurowany na COM1-COM4 (standardowo system wykorzystuje COM2),
  je�li b�dzie wykorzystywany dost�p przez PPP.
* Karta sieciowa: kompatybilna z NE2000, niekt�re typy 3Com, lub wersje PCI z
  chipsetem DEC21040 (drivery ed, ep i de), je�li b�dziesz korzysta� z dost�pu
  przez ethernet. Jest te� driver do karty PCI Intel EtherExpress (fxp), i 
  kart Lance/PCnet (lnc).

W jaki spos�b uzyska� dost�p dialup?
------------------------------------

Zalecam skorzystanie ze skryptu /stand/dialup, kt�ry skonfiguruje dodatkowo
us�ug� PPP w ten spos�b, �e b�dzie si� automatycznie ��czy� z providerem, oraz
ppp b�dzie dzia�a� w tle. Je�li jednak co� nie wyjdzie (daj mi zna� o tym!),
lub lubisz robi� to sam, oto opis poszczeg�lnych krok�w:

1.	wejd� do katalogu /etc/ppp i w pliku ppp.conf zmie� port
	szeregowy na ten, na kt�rym masz modem (cuaa0==COM1, cuaa1==COM2,
	itd...) Mo�esz to zrobi� edytorem 'ee /etc/ppp/ppp.conf'.

2.	uruchom program 'ppp'. Przejd� do trybu terminalowego (polecenie 
	'term').  W tym momencie masz bezpo�redni kontakt z modemem, wi�c
	normalnie wybierz numer dialup i zaloguj si� do serwera
	komunikacyjnego. Wydaj mu polecenie przej�cia w tryb ppp. Powiniene�
	zobaczy� co� takiego:

	(communication server...): ppp

	ppp on pico> Packet mode
	PPP on pico>

  	W tym momencie jeste� ju� online! Gratuluj�.
3.	Do Twojej dyspozycji s� nast�puj�ce programy: telnet, ftp, i ssh.
  	Poniewa� wywo�a�e� 'ppp' r�cznie, wi�c blokuje Ci konsol�. Nie
	szkodzi - masz do dyspozycji 9 kolejnych konsoli wirtualnych, po
	kt�rych mo�na si� porusza� naciskaj�c lewy Alt i klawisz funkcyjny
	F1-F10.

Jak skonfigurowa� kart� Ethernet?
---------------------------------

Miejmy nadziej�, �e Twoja karta jest obs�ugiwana przez j�dro dost�pne na
dyskietce, oraz �e poprawnie ustawi�e� jej parametry (w przypadku kart
ISA) w edytorze UserConfig. Mo�esz sprawdzi�, czy PicoBSD wykry�o t� kart�,
patrz�c na komunikaty startowe ('dmesg | more').

Naj�atwiejszym sposobem na skonfigurowanie dostepu LAN jest ustawienie
parametr�w w pliku konfiguracyjnym systemu ('ee /etc/rc.conf'). Znajd� lini�,
kt�ra zaczyna si� od 'network_interfaces' i dopisz nazw� sterownika karty do
listy interfejs�w. Potem dodaj jeszcze jedn� lini� 'ifconfig_<nazwa>',
kt�ra ustawi w�a�ciwy adres IP i mask� sieci. Np.:

	network_interfaces="lo0 ed0"
	ifconfig_lo0="inet 127.0.0.1"
	ifconfig_ed0="inet 192.168.0.1 netmask 255.255.255.0"

Nast�pnie musisz ustawi� sw�j domy�lny router (jest to zazwyczaj adres IP
routera w Twojej sieci LAN). Np.:

	defaultrouter="192.168.0.100"

Nast�pnie zachowujesz te informacje na dyskietce startowej przy pomocy
polecenia 'update', i restartujesz system.

Oczywi�cie mo�na te� zrobi� to r�cznie, wydaj�c odpowiednie polecenia:

	ifconfig ed0 inet 192.168.0.1 netmask 255.255.255.0
	route add default 192.168.0.100

Je�li poprawnie wszystko ustawi�e�, powiniene� by� w stanie uzyska�
odpowied� od swojego routera ('ping 192.168.0.100') oraz od jakiej�
maszyny poza Twoj� sieci�.

Skad wzi�� dodatkowe informacje?
--------------------------------

Oficjalna strona projektu PicoBSD:

	http://www.freebsd.org/~picobsd/

Mo�na tam znale�� troch� wi�cej informacji, oraz poprawki i nowe wersje.

Mi�ej zabawy!
  
Andrzej Bia�ecki <abial@freebsd.org>

$FreeBSD: src/release/picobsd/dial/lang/README.pl,v 1.5 1999/08/28 01:33:20 peter Exp $
