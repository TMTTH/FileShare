Description in English at the end of the document.

								Activation Program
					——————————————————————————————————————————————————

 Программа предназначена для off-line "активации" лицензий продуктов M$, в том числе и лицензий ESU/OCUR
на Windows 7. Программа НЕ "активирует" Office 2013-2019 на Windows 7 и Office 2010 на Windows 7-11.
В программе используется код требующий для работы установленный Net Framework 4.0 или выше.
В программе использован код от группы MASSGRAVE "TSforge (c) MASSGRAVE 2025".

								Работа с программой:
					——————————————————————————————————————————————————
	Работа с программой интуитивно понятна, выбираете лицензию и пробуете её "активировать". Программа сама
выбирает подходящий для данной лицензии метод, лицензии, которые можно активировать навсегда, "активируются" навсегда,
лицензии KMSCLIENT "активируются" на 4085 лет.

					Параметры запуска программы из командной строки:
					——————————————————————————————————————————————————
ActivationProgram.exe /act ID1 /act ID2 /act ID3 и так далее - Последовательно будет выполнена попытка "активировать"
	лицензии с указанными ID (ID - это идентификатор активации лицензии, в программе он отображается как SKU ID). 
	Метод активации программа выбирает сама.
ActivationProgram.exe /ipk KEY		- Устанавливает ключ продукта стандартным для системы методом.
ActivationProgram.exe /igpk ID		- Устанавливает сгенерированный/поддельный ключ продукта в соответствии с указанным идентификатором активации.	
ActivationProgram.exe /kms4k ID		- Активирует с помощью метода KMS4k. Поддерживает только активируемые KMS редакции.
ActivationProgram.exe /avma4k ID	- Активирует с помощью метода AVMA4k. Поддерживает только Windows Server 2012 R2+.
ActivationProgram.exe /zcid ID		- Активирует с помощью метода ZeroCID. Поддерживает только версии, активируемые с помощью телефона.
ActivationProgram.exe /rtmr ID		- Сбрасывает таймеры льготного периода/периода оценки.
ActivationProgram.exe /staticcid ID	- Активирует с помощью метода StaticCID.


                       Ответы на часто возникающие вопросы:
					——————————————————————————————————————————————————
1. Почему в описании программы слово активация взято в кавычки?
	Дело в том, что программа НЕ активирует продукт или лицензию, она изменяет запись об активации лицензии
	в "Надежном Хранилище" Windows. Во время обычной активации её результат помещается в это хранилище и
	потом никогда не перепроверяется, значит и наша измененная запись будет работать до переустановки системы
	или до её глобального обновления.
2. Я сохранил ключ из лога программы, попробовал его установить, slmgr.vbs /ipk КЛЮЧ, но он не устанавливается,
система пишет, что ключ неправилен.
	Всё так и есть, ключ неправилен, он нужен только для манипуляций с сохранением новых данных в хранилище.
3. При использовании программы требуется подключение к сети интернет?
	Нет, подключение к интернету не требуется, всё выполняется локально.
4. Я активировал лицензию KMSCLIENT, но она активировалась только на 180 дней, почему так произошло, где мои 4000 лет?
	Такое возможно, когда в системе есть запись о адресе и порте KMS-Service, попробуйте "активировать" с отключенным интернетом.
	
Ссылка на гитхаб, где можно больше прочитать об используемых в программе методах:
https://github.com/massgravel/TSforge 

 
Изменения в версиях :
v1.16.1
- Устранена ошибка отображения первой лицензии в списке.

v1.16
- Активация методом StaticCID добавлена в список параметров запуска.

v1.15
- Функция активации StaticCID интегрирована в пакет TSforge ©.
- У лицензий VOLUME_KMSCLIENT отображается дата и время конца действия лицензии.  

v1.14
- Заменен GVLK ключ Windows 7 Pro, в программе был не правильный.
- Функция активации StaticCID переписана. 

v1.13
- В названиях некоторых лицензий добавлено "[Not for all languages]". Например, на Windows 11 с русским MUI 
  эти лицензии активируются, но потом система не загружается.

v1.12
- Добавлен переключатель StaticCID. Без его установки метод ZeroCID не работает начиная с версии системы 19041.

v1.11
- Обновление ядра программы.

v1.10
- Изменения в работе программы с Windows Vista/Server 2008.

v1.09
- Увеличено количество встроенных в программу ключей.

v1.08
- В программу добавлена частичная поддержка активации Windows Vista и Server 2008. Генерировать ключи для
этих систем программа не умеет, но в неё уже встроены некоторые из них. Если вашего ключа нет, установите
его и потом активируйте.

v1.07
- Добавлена возможность установить свой ключ продукта.
- Можно изменять ширину окна программы.

v1.06
- Добавлено переключение вида списка лицензий "With add-on|No add-on|Add-on only"
- В окне "О программе" добавлена ссылка на сайт разработчика используемого в программе стороннего ПО.


v1.05
- В программу встроена база правильных GVLK ключей, фейковые ключи применяются лишь в отсутствии нормальных.
- При активации на 4085 лет в систему прописывается адрес несуществующего KMS-Service. Это нужно для нормальной работы Office.
- Для фейковых ключей удаляется UUID ключа продукта, используемый при онлайн-проверке ключа.


v1.03
- Отображается количество оставшихся дней до конца срока активации для KMSCLIENT лицензий.
- Если в имя файла программы добавить подстроку "dark", программа всегда будет запускаться в темной теме интерфейса.
  Пример: ActivationProgramDark.exe
- Наконец-то вместе с программой распространяется это исчерпывающее описание :)


Version in English:
							Activation Program
                 ——————————————————————————————————————————————————

	This program is designed for offline "activation" of Microsoft product licenses, including ESU/OCUR licenses
on Windows 7. The program does NOT "activate" Office 2013-2019 on Windows 7 and Office 2010 on Windows 7-11.
The program uses code that requires .NET Framework 4.0 or higher to work. 
The code from the group MASSGRAVE "TSforge (c) MASSGRAVE 2025" has been used in this program.


                         Working with the program:
             ——————————————————————————————————————————————————
	Working with the program is intuitive: you select a license and try to "activate" it. The program itself selects 
an appropriate method for that particular license. Licenses that can be activated permanently are "activated" permanently,
while KMSCLIENT licenses are "activated" for 4085 years.


               Command line parameters for launching the program:
             ——————————————————————————————————————————————————
ActivationProgram.exe /act ID1 /act ID2 /act ID3 etc. - An attempt will be made to sequentially "activate" licenses 
	with the specified IDs (ID is the activation identifier of the license, which is displayed in the program as SKU ID).
	The program chooses the activation method itself.
ActivationProgram.exe /ipk KEY		- Install the product key with standard method for the system.
ActivationProgram.exe /igpk ID		- Install auto-generated/fake product key according to the specified Activation ID.
ActivationProgram.exe /kms4k ID		- Activate using KMS4k. Only supports KMS-activatable editions.
ActivationProgram.exe /avma4k ID	- Activate using AVMA4k. Only supports Windows Server 2012 R2+.
ActivationProgram.exe /zcid ID		- Activate using ZeroCID. Only supports phone-activatable editions.
ActivationProgram.exe /rtmr ID		- Reset grace/evaluation period timers.
ActivationProgram.exe /staticcid ID	- Activate using StaticCID.

                    Answers to frequently asked questions:
             ——————————————————————————————————————————————————
1. Why is the word "activation" in quotes in the program description?
	The fact is that the program does not actually activate the product or license, instead, it modifies the activation 
	record in the Windows "Trusted Storage." During normal activation, its result is placed in this storage and never 
	rechecked again, so our modified entry will work until the system is reinstalled or undergoes a major update.
2. I saved the key from the program log, tried to install it using slmgr.vbs /ipk KEY, but it doesn't install,
the system says the key is incorrect.
	That's correct, the key is indeed invalid. It is only needed for manipulating new data in the storage.
3. Does using the program require an internet connection?
	No, an internet connection is not required, everything is done locally.
4. I activated a KMSCLIENT license, but it was only activated for 180 days. Why did this happen, where are my 4000 years?
	This may occur when there is a record of the address and port of the KMS-Service in the system. Try "activating" with the
	internet disabled.
	
Link to GitHub, where you can read more about the methods used in the program:
https://github.com/massgravel/TSforge

