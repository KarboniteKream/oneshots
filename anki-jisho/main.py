from bs4 import BeautifulSoup
import genanki
import random
import requests
import sys


class KanjiNote(genanki.Note):
    @property
    def guid(self):
        return genanki.guid_for(self.fields[0])


def get_id():
    return random.randrange(1 << 30, 1 << 31)


def get_list(tag):
    kanji = []
    url = 'https://jisho.org/search/%23kanji%20%23' + tag

    while url:
        request = requests.get(url)
        soup = BeautifulSoup(request.text, 'html.parser')

        for entry in soup.find_all('span', class_='character'):
            kanji.append((entry.a.string, 'http:' + entry.a['href']))

        next_page = soup.find('a', class_='more')

        if next_page:
            url = 'https:' + next_page['href']
        else:
            url = None

    return kanji


def get_kanji(name, url):
    print(name)

    request = requests.get(url)
    soup = BeautifulSoup(request.text, 'html.parser')

    kanji = soup.find('h1', class_='character').string
    strokes = soup.find('div', class_='kanji-details__stroke_count')
    strokes = strokes.strong.string
    radicals = soup.find_all('div', class_='radicals')
    radical = ' '.join(radicals[0].find('span').text.split())
    parts = ', '.join(map(lambda x: x.string, radicals[1].find_all('a')))

    readings = soup.find('div', class_='kanji-details__main-readings')
    onyomi = readings.find('dl', class_='on_yomi')
    if onyomi:
        onyomi = onyomi.dd.find_all('a')
        onyomi = ', '.join(map(lambda x: x.string, onyomi))
    else:
        onyomi = ''

    kunyomi = readings.find('dl', class_='kun_yomi')
    if kunyomi:
        kunyomi = kunyomi.dd.find_all('a')
        kunyomi = ', '.join(map(lambda x: x.string, kunyomi))
    else:
        kunyomi = ''

    meaning = soup.find('div', class_='kanji-details__main-meanings')
    meaning = meaning.string.strip()

    examples = soup.find('div', class_='compounds').find_all('div')
    examples = [y for x in examples for y in x.find_all('li')[:2]]
    examples = '<br />'.join([' '.join(x.string.split()) for x in examples])

    return [str(field) for field in [
        kanji, '', strokes, radical, parts,
        onyomi, kunyomi, meaning, examples
    ]]


qfmt = '''
<div class="kanji">{{Kanji}}</div>
'''.strip()

afmt = '''
<div class="kanji">{{Kanji}}</div>
<div class="reading">{{On'yomi}}</div>
<div class="reading">{{Kun'yomi}}</div>

<hr />

{{Meaning}}

<div class="diagram">{{Diagram}}</div>
<div class="examples">{{Examples}}</div>

<hr />

<div class="details">
    <div>{{Strokes}} strokes</div>
    <div>Radical: {{Radical}}</div>
    <div>Parts: {{Parts}}</div>
</div>
'''.strip()

css = '''
.card {
    font-family: Arial;
    font-size: 20px;
    text-align: center;
    color: black;
    background-color: white;
}

.kanji {
    font-size: 40px;
}

.reading,
.examples,
.details {
    font-size: 15px;
}

.diagram img {
    display: inline-block;
    width: 33%;
}
'''.strip()

model = genanki.Model(
    1107557011,
    'Kanji [Japanese]',
    fields=[
        {'name': 'Kanji'},
        {'name': 'Diagram'},
        {'name': 'Strokes'},
        {'name': 'Radical'},
        {'name': 'Parts'},
        {'name': 'On\'yomi'},
        {'name': 'Kun\'yomi'},
        {'name': 'Meaning'},
        {'name': 'Examples'},
    ],
    templates=[
        {
            'name': 'Card 1',
            'qfmt': qfmt,
            'afmt': afmt,
        },
    ],
    css=css,
)

if len(sys.argv) < 2:
    print('Please specify JLPT level (N5, N4, N3, N2, N1)')
    sys.exit(1)

level = sys.argv[1].lower()
deck_name = '[{}] Kanji'.format(level.upper())
file_name = 'jlpt-{}.apkg'.format(level)

deck = genanki.Deck(get_id(), deck_name)
kanji = [get_kanji(*kanji) for kanji in get_list('jlpt-' + level)]

for fields in kanji:
    note = KanjiNote(model=model, fields=fields)
    deck.add_note(note)

genanki.Package(deck).write_to_file(file_name)
