name = aodv-uu-implementation
src = $(name).tex

.PHONY: default gv clean all

default: $(name).ps

all: $(name).ps $(name).pdf
index: $(name).idx

$(name).dvi: $(src)
	latex $(src); latex $(src)
	$(MAKE) $(name).idx

$(name).idx: $(name).dvi $(src)
	latex $(src)

$(name).ps: $(name).dvi
	dvips -o $(name).ps $(name).dvi

$(name).pdf: $(name).ps
	ps2pdf $(name).ps $(name).pdf

gv: $(name).ps
	gv $(name).ps &

clean:
	rm -f *.log *.ps *.pdf *.aux *.dvi *~
