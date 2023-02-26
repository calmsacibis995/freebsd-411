#!/bin/sh
# $FreeBSD: src/release/picobsd/dial/lang/dialup.pl,v 1.5 1999/08/28 01:33:20 peter Exp $
set_resolv() {
	echo "[H[J"
	echo "[1m                       Domy�lna Nazwa Domeny[m"
	echo ""
	echo "Podaj domy�ln� nazw� domeny Internetowej, kt�rej b�dziesz u�ywa�."
	echo "Je�li Tw�j provider ma nazwy typu 'www.akuku.com.pl', to b�dzie"
	echo "to najprawdopodobniej 'akuku.com.pl'."
	echo ""
	echo "Je�li po prostu naci�niesz Enter, ustawisz (nieistniej�c�) domen�"
	echo "'mydomain.org.pl', co nie jest najlepszym pomys�em, ale mo�e na"
	echo "razie wystarczy�."
	echo ""
	read -p "Podaj domy�ln� nazw� domeny: " domain
	if [ "X${domain}" = "X" ]
	then
		echo ""
		echo "Dobrze, ustawimy 'mydomain.org.pl', ale miej �wiadomo��"
		echo "�e taka domena prawdopodobnie nie istnieje."
		echo ""
		read -p "Naci�nij Enter" junk
		domain="mydomain.org.pl"
	fi
	echo "[H[J"
	echo "[1m                      Adres Serwera DNS[m"
	echo ""
	echo "Podaj adres w postaci numerycznej serwera DNS. Jest on potrzebny"
	echo "do zamiany nazw (takich jak www.freebsd.org.pl) na adresy IP"
	echo "(takie jak 192.168.1.1). Je�li nie jest to ustawione poprawnie,"
	echo "b�dziesz musia� pos�ugiwa� si� adresami IP podczas ��czenia si�"
	echo "z innymi maszynami - jest to co najmniej niewygodne."
	echo ""
	echo "Je�li po prostu naci�niesz Enter, ustawisz (istniej�cy) serwer"
	echo "o numerze 194.204.159.1 (w sieci TP SA)."
	echo ""
	read -p "Podaj adres IP serwera DNS (w postaci A.B.C.D): " dns
	if [ "X${dns}" = "X" ]
	then
		echo ""
		echo "Dobrze, ustawimy adres DNS serwera na 194.204.159.1, ale"
		echo "niekoniecznie musi to by� najlepszy serwer w Twojej cz�ci sieci."
		echo ""
		read -p "Naci�nij Enter..." junk
		dns="194.204.159.1"
	fi
}
set_phone() {
while [ "X${phone}" = "X" ]
do
	echo "[H[J"
	echo "[1m                        Numer Telefoniczny[m"
	echo ""
	echo "Podaj numer telefoniczny, kt�rego normalnie u�ywasz, �eby"
	echo "dodzwoni� si� do swojego providera. Powiniene� poda� pe�ny"
	echo "numer, z ewentualnymi przedrostkami, np: 022113355"
	echo ""
	read -p "Podaj numer telefoniczny: " phone
done
}

set_port() {
while [ "X${dev}" = "X" ]
do
	echo "[H[J"
	echo "[1m                        Numer Portu Modemowego[m"
	echo ""
	echo "Podaj numer portu szeregowego, do kt�rego pod��czony jest modem."
	echo "UWAGA: DOSowy port COM1 to port 0 (cuaa0) we FreeBSD, COM2 -"
	echo "port 1, itd. Podaj tutaj tylko numer, a nie pe�n� nazw� urz�dzenia."
	echo ""
	read -p "Podaj numer portu szeregowego (0,1,2): " dev
done
}

set_speed() {
while [ "X${speed}" = "X" ]
do
	echo "[H[J"
	echo "[1m                      Pr�dko�� Linii Szeregowej[m"
	echo ""
	echo "Wybierz pr�dko�� linii szeregowej, kt�rej u�ywa modem."
	echo ""
	echo "UWAGA: Pr�dko�� linii szeregowej NIE jest tym samym, co pr�dko��"
	echo "modemu. Je�li Tw�j modem obs�uguje protok� V.42 lub MNP"
	echo "(zazwyczaj tak w�a�nie jest), pr�dko�� linii szeregowej musi by�"
	echo "du�o wi�ksza od pr�dko�ci modemu. Np. dla modem�w 14.4 kbps z"
	echo "kompresj� nale�y wybra� pr�dko�� 38400 bps, a dla modem�w"
	echo "28.8 kbps z kompresj� nale�y wybra� pr�dko�� 115200 bps."
	echo ""
	echo "	1.	9600   bps"
	echo "	2.	14400  bps"
	echo "	3.	28800  bps"
	echo "	4.	38400  bps (modem 14.4 kbps z kompresj�)"
	echo "	5.	57600  bps"
	echo "	6.	115200 bps (modem 28.8 kbps z kompresj�)"
	echo ""
	read -p "Wybierz pr�dko�� linii szeregowej (1-6): " ans
	case ${ans} in
	1)
		speed=9600
		;;
	2)
		speed=14400
		;;
	3)
		speed=28800
		;;
	4)
		speed=38400
		;;
	5)
		speed=57600
		;;
	6)
		speed=115200
		;;
	*)
		read -p "Z�a warto��! Naci�nij Enter..." junk
		unset speed
		;;
	esac
done
}

set_timeout() {
while [ "X${timo}" = "X" ]
do
	echo "[H[J"
	echo "[1m                        Czas roz��czenia[m"
	echo ""
	echo "Podaj czas (w sekundach), po kt�rym, je�li nie ma ruchu na ��czu,"
	echo "nast�pi automatyczne roz��czenie. To pomaga w oszcz�dzaniu :-)"
	echo ""
	read -p "Podaj czas roz��czenia: " timo
done
}

set_user() {
while [ "X${user}" = "X" ]
do
	echo "[H[J"
	echo "[1m                        Nazwa U�ytkownika[m"
	echo ""
	echo "Podaj nazw� u�ytkownika (login name), kt�rej normalnie u�ywasz"
	echo "do zalogowania si� do serwera komunikacyjnego providera."
	echo ""
	read -p "Podaj nazw� u�ytkownika: " user
done
}

set_pass() {
while [ "X${pass}" = "X" ]
do
	echo "[H[J"
	echo "[1m                        Has�o[m"
	echo ""
	echo "Podaj has�o, kt�rego u�ywasz do zalogowania si� do providera."
	echo ""
	echo "[31mUWAGA: Has�o to zostanie zapisane w czytelnej postaci na"
	echo "dyskietce!!! Je�li tego nie chcesz... b�dziesz musia� logowa� si�"
	echo "r�cznie, tak jak dotychczas. W tym przypadku przerwij ten skrypt"
	echo "przez Ctrl-C.[37m"
	echo ""
	stty -echo
	read -p "Podaj swoje has�o: " pass
	echo ""
	read -p "Podaj powt�rnie swoje has�o: " pass1
	stty echo
	echo ""
	if [ "X${pass}" != "X${pass1}" ]
	then
		echo "Has�a nie pasuj� do siebie. Naci�nij Enter..."
		pass=""
		read junk
		set_pass
	fi
done
}

set_chat() {
echo "[H[J"
while [ "X${chat}" = "X" ]
do
	echo "[1m               Rodzaj dialogu podczas logowania si�[m"
	echo ""
	echo "Jak normalnie przebiega proces logowania si� do serwera"
	echo "komunikacyjnego?"
	echo ""
	echo "1)	[32m......login:[37m ${user}"
	echo "	[32m...password:[37m ********"
	echo "		[36m(tutaj startuje PPP)[37m"
	echo ""
	echo "2)	[32m...username:[37m ${user}			(TP S.A.)"
	echo "	[32m...password:[37m ********"
	echo "		[36m(tutaj startuje PPP)[37m"
	echo ""
	echo "3)	[32m......username:[37m ${user}			(NASK)"
	echo "	[32m......password:[37m ********"
	echo "	[32mportX/..xxx...:[37m ppp"
	echo "		[36m(tutaj startuje PPP)[37m"
	echo ""
	echo "4)	[32mZastosuj CHAP[37m"
	echo ""
	echo "5)	[32mZastosuj PAP[37m"
	echo ""
	read -p "Wybierz 1,2,3,4 lub 5: " chat
	case ${chat} in
	1)
		chat1="TIMEOUT 10 ogin:--ogin: ${user} word: \\\\P"
		chat2="login/password"
		;;
	2)
		chat1="TIMEOUT 10 ername:--ername: ${user} word: \\\\P"
		chat2="TP SA - username/password"
		;;
	3)
		chat1="TIMEOUT 10 ername:--ername: ${user} word: \\\\P port ppp"
		chat2="NASK - username/password/port"
		;;
	4)	chat1="-"
		chat2="CHAP"
		;;
	5)	chat1="-"
		chat2="PAP"
		;;
	*)	echo "Z�a warto��! Musisz wybra� 1,2 lub 3."
		echo ""
		unset chat
		unset chat2
		;;
	esac
done
}


# Main entry of the script

echo "[H[J"
echo "[1m              Witamy w Automatycznym Konfiguratorze PPP! :-)[m"
echo ""
echo "    PPP jest ju� wst�pnie skonfigurowane, tak �e mo�na r�cznie wybiera�"
echo "numer i r�cznie logowa� si� do serwera komunikacyjnego. Jest to jednak"
echo "dosy� uci��liwy spos�b na d�u�sz� met�."
echo ""
echo "Ten skrypt postara si� stworzy� tak� konfiguracj� PPP, �eby umo�liwi�"
echo "automatyczne wybieranie numeru i logowanie si�, a ponadto pozwoli na"
echo "uruchamianie ppp w tle - nie zajmuje ono w�wczas konsoli."
echo ""
echo "Je�li chcesz kontynuowa�, naci�nij [1mEnter[m, je�li nie - [1mCtrl-C[m."
echo ""
read junk
# Step through the options
set_phone
set_port
set_speed
set_timeout
set_user
set_pass
set_chat
set_resolv

ans="loop_it"
while [ "X${ans}" != "X" ]
do

echo "[H[J"
echo "[1m     Ustawione zosta�y nast�puj�ce parametry:[m"
echo ""
echo "	1.	Numer telef.:	${phone}"
echo "	2.	Numer portu:	cuaa${dev}"
echo "	3.	Pr�dko�� portu:	${speed}"
echo "	4.	Czas roz��cz.:	${timo} s"
echo "	5.	U�ytkownik:	${user}"
echo "	6.	Has�o:		${pass}"
echo "	7.	Typ dialogu:	${chat} (${chat2})"
echo "	8.	Nazwa domeny:	${domain}"
echo "		Serwer DNS:	${dns}"
echo ""
echo "Je�li te warto�ci s� poprawne, po prostu naci�nij [1mEnter[m"
read -p "Je�li nie, podaj numer opcji, kt�r� chcesz zmieni� (1-8): " ans

a="X${ans}"
case ${a} in
X1)
	unset phone
	set_phone
	;;
X2)
	unset dev
	set_port
	;;
X3)
	unset speed
	set_speed
	;;
X4)
	unset timo
	set_timeout
	;;
X5)
	unset user
	set_user
	;;
X6)
	unset pass
	set_pass
	;;
X7)
	unset chat
	unset chat1
	unset chat2
	set_chat
	;;
X8)
	unset domain
	unset dns
	set_resolv
	;;
X)
	;;
*)
	read -p "Z�y numer opcji! Naci�nij Enter..." junk
	ans="wrong"
	;;
esac
done

echo ""
echo -n "Generowanie /etc/ppp/ppp.conf file..."
rm -f /etc/ppp/ppp.conf
cp /etc/ppp/ppp.conf.template /etc/ppp/ppp.conf
echo "" >>/etc/ppp/ppp.conf
echo "# This part was generated with $0" >>/etc/ppp/ppp.conf
echo "dialup:" >>/etc/ppp/ppp.conf
echo " set line /dev/cuaa${dev}" >>/etc/ppp/ppp.conf
echo " set phone ${phone}" >>/etc/ppp/ppp.conf
echo " set authkey ${pass}" >>/etc/ppp/ppp.conf
echo " set timeout ${timo}" >>/etc/ppp/ppp.conf
if [ "X${chat1}" = "-" ]
then
	echo "set authname ${user}" >>/etc/ppp/ppp.conf
else
	echo " set login \"${chat1}\"" >>/etc/ppp/ppp.conf
fi
echo " set ifaddr 10.0.0.1/0 10.0.0.2/0 255.255.255.0 0.0.0.0" >>/etc/ppp/ppp.conf

echo " Zrobione."

echo -n "Generowanie /etc/resolv.conf..."
echo "# This file was generated with $0">/etc/resolv.conf
echo "domain ${domain}" >>/etc/resolv.conf
echo "nameserver ${dns}">>/etc/resolv.conf
echo "hostname=\"pico.${domain}\"">>/etc/rc.conf
echo " Zrobione."

echo ""
echo "Ok. Sprawd� zawarto�� /etc/ppp/ppp.conf, i popraw go je�li to konieczne."
echo "Nast�pnie mo�esz wystartowa� ppp w tle:"
echo ""
echo "	[1mppp -background dialup[m"
echo ""
echo "PAMI�TAJ, �eby uruchomi� /stand/update ! Inaczej zmiany nie zostan� zapisane"
echo "na dyskietce!"
echo ""
echo "Ok. Je�li Tw�j plik /etc/ppp/ppp.conf jest prawid�owy (co jest dosy�"
echo -n "prawdopodobne :-), czy chcesz teraz uruchomi� po��czenie dialup? (t/n) "
read ans
opts=""
while [ "X${ans}" = "Xt" ]
do
	echo "[H[J"
	if [ "X${opts}" = "X" ]
	then
		echo "Wystartujemy 'ppp' z poni�szymi opcjami:"
		echo ""
		echo "		ppp -background dialup"
		echo ""
		echo -n "Czy chcesz je zmienic?? (t/n) "
		read oo
		if [ "X${oo}" = "Xt" ]
		then
			read -p "Podaj opcje ppp: " opts
		else
			opts="-background dialup"
		fi
		echo ""
		echo ""
	fi
	echo "Uruchamiam po��czenie dialup. Prosz� czeka� dop�ki nie pojawi si�"
	echo "komunikat 'PPP Enabled'..."
	echo ""
	ppp -background dialup
	if [ "X$?" != "X0" ]
	then
		echo -n "Po��czenie nie powiod�o si�. Spr�bowa� jeszcze raz?  (t/n) "
		read ans
		if [ "X${ans}" != "Xt" ]
		then
			echo "Spr�buj p�niej. Sprawd� r�wnie� plik konfiguracyjny /etc/ppp/ppp.conf."
			echo ""
		fi
	else
		echo ""
		echo "Gratuluj�! Jeste� on-line."
		echo ""
		exit 0
	fi
done
