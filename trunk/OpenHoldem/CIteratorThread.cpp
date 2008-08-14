#include "stdafx.h"
#include <process.h>

#include "CScraper.h"
#include "CSymbols.h"
#include "grammar.h"
#include "CIteratorThread.h"
#include "inlines/eval.h"

CIteratorThread		*p_iterator_thread = NULL;
CRITICAL_SECTION	cs_iterator;

SIterVars			_iter_vars;
SIterParams			_iter_params;

CIteratorThread::CIteratorThread()
{
	__SEH_SET_EXCEPTION_HANDLER(MyUnHandledExceptionFilter);

	__SEH_HEADER

	// Create events
	_m_stop_thread = CreateEvent(0, TRUE, FALSE, 0);
	_m_wait_thread = CreateEvent(0, TRUE, FALSE, 0);

	// Initialize variables
	InitIteratorLoop();

	// Start thread
	AfxBeginThread(IteratorThreadFunction, this);

	EnterCriticalSection(&cs_iterator);
	_iter_vars.iterator_thread_running = true;
	LeaveCriticalSection(&cs_iterator);

	__SEH_LOGFATAL("CIteratorThread::Constructor :\n");
}

CIteratorThread::~CIteratorThread()
{
	__SEH_HEADER

	// Trigger thread to die
	::SetEvent(_m_stop_thread);

	// Wait until thread finished
	::WaitForSingleObject(_m_wait_thread, INFINITE);

	// Close handles
	::CloseHandle(_m_stop_thread);
	::CloseHandle(_m_wait_thread);

	p_iterator_thread = NULL;

	__SEH_LOGFATAL("CIteratorThread::Destructor :\n");
}

UINT CIteratorThread::IteratorThreadFunction(LPVOID pParam)
{
	__SEH_SET_EXCEPTION_HANDLER(MyUnHandledExceptionFilter);

	__SEH_HEADER

	CIteratorThread *pParent = static_cast<CIteratorThread*>(pParam);

	int				i, j, k, randfix, nit = 0; //modified by Matrix 2008-05-08
	CardMask		addlcomCards, evalCards, usedCards, temp_usedCards;
	unsigned int	ocard[MAX_OPPONENTS*2], card, pl_pokval, opp_pokval, opp_pokvalmax;
	HandVal			pl_hv, opp_hv;
	double			dummy = 0;
	unsigned int	deck[52], x, swap;
	int				numberOfCards;

	int				sym_br = (int) p_symbols->sym()->br;
	int				sym_playersplayingbits = (int) p_symbols->sym()->playersplayingbits;
	double			sym_nbetsround = p_symbols->sym()->nbetsround[4];
	int				sym_bblindbits = (int) p_symbols->sym()->bblindbits;
	bool			sym_didcall = (bool) p_symbols->sym()->didcall;
	int				sym_nopponents = (int) p_symbols->sym()->nopponents;

	int				nopp = sym_nopponents <= MAX_OPPONENTS ? sym_nopponents : MAX_OPPONENTS;

	//
	// Main iterator loop
	//
	for (nit=0; nit < _iter_params.nit; nit++)
	{
		// Check event for thread stop signal
		if(::WaitForSingleObject(pParent->_m_stop_thread, 0) == WAIT_OBJECT_0)
		{
			// Set event
			::SetEvent(pParent->_m_wait_thread);
			AfxEndThread(0);
		}

		CardMask_OR(usedCards, pParent->_plCards, pParent->_comCards);

		if (pParent->_willplay && (pParent->_willplay<nopp+1) )
			pParent->_willplay=nopp+1; //too low a value can give lockup


		if (p_symbols->prw1326()->useme==1326 && (sym_br!=1 || p_symbols->prw1326()->preflop==1326))
		{
			//prw1326 active  Matrix 2008-05-08
			k = nopp = 0; //k is used as an index into ocard[] 

			// loop through active opponents
			for(i=0; i<=9; i++) 
			{
				if (i==(int) p_symbols->sym()->userchair)
					continue; //skip our own chair!

				if (!((sym_playersplayingbits) & (1<<i)))
					continue; //skip inactive chairs 

				nopp++; //we have to use actual opponents for prw1326 calculations

				// first deal with the special non-weighted cases
				// player who is marked 'ignore' or one who is BB and has not VPIP'd
				if ( p_symbols->prw1326()->chair[i].ignore || 
					(p_symbols->prw1326()->bblimp && sym_nbetsround<1.1 && (sym_bblindbits&(1<<i))) )
				{
					do 
					{
						card = rand() & 63;
					}
					while (card>51 || CardMask_CARD_IS_SET(usedCards, card));
					
					CardMask_SET(usedCards, card);
					ocard[k++] = card;

					do 
					{
						card = rand() & 63;
					}
					while (card>51 || CardMask_CARD_IS_SET(usedCards, card));
					
					CardMask_SET(usedCards, card);
					ocard[k++] = card;
					continue;
				} // end of special non-weighted cases

				randfix=(RAND_MAX/p_symbols->prw1326()->chair[i].limit) * p_symbols->prw1326()->chair[i].limit;


				while (true)
				{ //find a possible hand for this chair NOTE: may want to put in loop limits to prevent hanging
					do 
					{
						j=rand();
					} while (j>=randfix);

					j = j % p_symbols->prw1326()->chair[i].limit; //j is now any one of the allowed hands

					if(CardMask_CARD_IS_SET(usedCards, p_symbols->prw1326()->chair[i].rankhi[j] ))
						continue; //hand contains dead card

					if(CardMask_CARD_IS_SET(usedCards, p_symbols->prw1326()->chair[i].ranklo[j] ))
						continue; //hand contains dead card

//					if(symbols.prw1326.chair[i].ignore)break; //chair marked as not to be weighted

					if(p_symbols->prw1326()->chair[i].level <= p_symbols->prw1326()->chair[i].weight[j])
						break; //hand marked as always uae

					//check if we want a player who is BB and has not VPIP'd to be analysed further
//					if(symbols.prw1326.bblimp)
//					{
//					if ((symbols.sym.nbetsround[0]<1.1) && ((int)symbols.sym.bblindbits&(1<<i)))break;
//					}

					//we should really do a 'randfix' here for the case where RAND_MAX is not an integral
					//multiple of .level, but the bias introduced is trivial compared to other uncertainties.
					if(rand() % p_symbols->prw1326()->chair[i].level < p_symbols->prw1326()->chair[i].weight[j])
						break; //allowable

					//if we reach here we will loop again to find a suitable hand
				} //end of possible hand find

				ocard[k++] = p_symbols->prw1326()->chair[i].rankhi[j];
				ocard[k++] = p_symbols->prw1326()->chair[i].ranklo[j];

				CardMask_SET(usedCards, ocard[k-2]);
				CardMask_SET(usedCards, ocard[k-1]);

			} //end of active opponent loop

			// additional common cards
			CardMask_RESET(addlcomCards);
			for (i=0; i<(5 - pParent->_ncomCards); i++)
			{
				do {
					card = rand() & 63;
				}
				while (card>51 ||CardMask_CARD_IS_SET(usedCards, card));
				CardMask_SET(usedCards, card);
				CardMask_SET(addlcomCards, card);
			}
		} //end of prw1326 code
		
		else
		{ //normal prwin opponent card selection
		  // if f$P<=13 then deal with random replacement algorithm,
			// otherwise deal with swap algorithm
			if (nopp <= 13)
			{
				// random replacement algorithm
				// opponent cards
				for (i=0; i<nopp*2; i+=2)
				{
					temp_usedCards=usedCards;
					do
					{
						usedCards=temp_usedCards; //reset the card mask to clear settings from failed card assignments

						do {
							card = rand() & 63;
						}
						while (card>51 || CardMask_CARD_IS_SET(usedCards, card));
						CardMask_SET(usedCards, card);
						ocard[i] = card;

						do {
							card = rand() & 63;
						}
						while (card>51 || CardMask_CARD_IS_SET(usedCards, card));
						CardMask_SET(usedCards, card);
						ocard[i+1] = card;

						if (!pParent->_willplay)
							break; //0 disables weighting

						//put break for i=0 and opponent unraised BB case (cannot assume anything about his cards)
						//In round 1 we should really do an analysis of chairs to find out how many have still to
						//place a bet. Not implemented since accuracy of prwin pre-flop is less critical.
						if (!i)
						{
							//if we called then we are not BB, BB limped to flop,
							//BB still playing, so do not weight his cards
							if (sym_nbetsround<1.1 && sym_didcall && (sym_playersplayingbits&sym_bblindbits) )
							{
								break;
							}
						}
					}
					while (!pParent->InRange(ocard[i],ocard[i+1]));
				}
				// additional common cards
				CardMask_RESET(addlcomCards);
				for (i=0; i<(5 - pParent->_ncomCards); i++)
				{
					do {
						card = rand() & 63;
					}
					while (card>51 ||CardMask_CARD_IS_SET(usedCards, card));
					CardMask_SET(usedCards, card);
					CardMask_SET(addlcomCards, card);
				}
			}

			else
			{
				// swap alogorithm
				//weighted prwin not implemented for this case
				numberOfCards=52;
				for (i=0; i<numberOfCards; i++)
					deck[i] = i;

				while (numberOfCards>=1)
				{
					x = rand() % numberOfCards;
					numberOfCards--;
					if (x != numberOfCards)
					{
						swap = deck[x];
						deck[x] = deck[numberOfCards];
						deck[numberOfCards] = swap;
					}
				}

				// opponent cards
				x = 0;
				for (i=0; i<nopp*2; i++)
				{
					while (CardMask_CARD_IS_SET(usedCards, deck[x]) && x<=51) {
						x++;
					}
					ocard[i] = deck[x++];
				}

				// additional common cards
				CardMask_RESET(addlcomCards);
				for (i=0; i<(5 - pParent->_ncomCards); i++)
				{
					while (CardMask_CARD_IS_SET(usedCards, deck[x]) && x<=51) {
						x++;
					}
					CardMask_SET(addlcomCards, deck[x++]);
				}
			}
		}

		// Get my handval/pokerval
		CardMask_OR(evalCards, pParent->_plCards, pParent->_comCards);
		CardMask_OR(evalCards, evalCards, addlcomCards);
		pl_hv = Hand_EVAL_N(evalCards, 7);
		pl_pokval = p_symbols->CalcPokerval(pl_hv, 7, &dummy, CARD_NOCARD, CARD_NOCARD);

		// Scan through opponents' handvals/pokervals
		// - if we find one better than ours, then we are done, increment los
		// - for win/tie, we need to wait until we scan them all
		opp_pokvalmax = 0;
		for (i=0; i<nopp; i++)
		{
			CardMask_OR(evalCards, pParent->_comCards, addlcomCards);
			CardMask_SET(evalCards, ocard[i*2]);
			CardMask_SET(evalCards, ocard[(i*2)+1]);
			opp_hv = Hand_EVAL_N(evalCards, 7);
			opp_pokval = p_symbols->CalcPokerval(opp_hv, 7, &dummy, CARD_NOCARD, CARD_NOCARD);

			if (opp_pokval>pl_pokval)
			{
				pParent->_los++;
				i = nopp+999;
			}
			else
			{
				if (opp_pokval > opp_pokvalmax)
					opp_pokvalmax = opp_pokval;
			}
		}
		if (i!=nopp+1000)
		{
			if (pl_pokval > opp_pokvalmax)
				pParent->_win++;
			else
				pParent->_tie++;
		}

		if ((nit/1000 == (int) nit/1000) && nit>=1000)
		{
			EnterCriticalSection(&cs_iterator);
				_iter_vars.iterator_thread_progress = nit;
				_iter_vars.prwin = pParent->_win / (double) nit;
				_iter_vars.prtie = pParent->_tie / (double) nit;
				_iter_vars.prlos = pParent->_los / (double) nit;
			LeaveCriticalSection(&cs_iterator);
		}
	}

	if (nit >= _iter_params.nit)
	{
		EnterCriticalSection(&cs_iterator);
			_iter_vars.iterator_thread_running = false;
			_iter_vars.iterator_thread_complete = true;
			_iter_vars.iterator_thread_progress = nit;
			_iter_vars.prwin = pParent->_win / _iter_params.nit;
			_iter_vars.prtie = pParent->_tie / _iter_params.nit;
			_iter_vars.prlos = pParent->_los / _iter_params.nit;
		LeaveCriticalSection(&cs_iterator);
	}
	else
	{
		EnterCriticalSection(&cs_iterator);
			_iter_vars.iterator_thread_running = false;
			_iter_vars.iterator_thread_complete = false;
			_iter_vars.iterator_thread_progress = 0;
			_iter_params.nit = 0;
			_iter_params.f$p = 0;
			_iter_params.br = 0;
			for (i=0; i<=1; i++)
				_iter_params.pcard[i] = CARD_NOCARD;
			for (i=0; i<=4; i++)
				_iter_params.ccard[i] = CARD_NOCARD;

			_iter_vars.prwin = 0.;
			_iter_vars.prtie = 0.;
			_iter_vars.prlos = 0.;
		LeaveCriticalSection(&cs_iterator);
	}

	::SetEvent(pParent->_m_wait_thread);
	delete p_iterator_thread;

	return 0;

	__SEH_LOGFATAL("CIteratorThread::IteratorThreadFunction :\n");
}

void CIteratorThread::InitIteratorLoop()
{
	__SEH_HEADER

	int i, e;
	
	// Set starting status and parameters
	EnterCriticalSection(&cs_iterator);
		_iter_vars.iterator_thread_running = true;
		_iter_vars.iterator_thread_complete = false;
		_iter_vars.iterator_thread_progress = 0;
		_iter_params.f$p = (int) p_symbols->sym()->nopponents;
		_iter_params.nit = (int) p_symbols->sym()->nit;
		_iter_params.br = (int) p_symbols->sym()->br;
		_iter_params.pcard[0] = p_scraper->card_player((int) p_symbols->sym()->userchair, 0);
		_iter_params.pcard[1] = p_scraper->card_player((int) p_symbols->sym()->userchair, 1);
		_iter_params.ccard[0] = p_scraper->card_common(0);
		_iter_params.ccard[1] = p_scraper->card_common(1);
		_iter_params.ccard[2] = p_scraper->card_common(2);
		_iter_params.ccard[3] = p_scraper->card_common(3);
		_iter_params.ccard[4] = p_scraper->card_common(4);
		_iter_vars.prwin = _iter_vars.prtie = _iter_vars.prlos = 0.;
	LeaveCriticalSection(&cs_iterator);

	// Seed RNG
	srand((unsigned)time( NULL ));

	// player cards
	CardMask_RESET(_plCards);
	CardMask_RESET(_comCards);
	_nplCards = _ncomCards = 0;

	// Counters
	_win = _tie = _los = 0;

	// setup masks
	for (i=0; i<=1; i++)
	{
		if (_iter_params.pcard[i] != CARD_BACK && _iter_params.pcard[i] != CARD_NOCARD)
		{
			CardMask_SET(_plCards, _iter_params.pcard[i]);
			_nplCards++;
		}
	}
	for (i=0; i<=4; i++)
	{
		if (_iter_params.ccard[i] != CARD_BACK && _iter_params.ccard[i] != CARD_NOCARD)
		{
			CardMask_SET(_comCards, _iter_params.ccard[i]);
			_ncomCards++;
		}
	}

	//Weighted prwin only for nopponents <=13
	e = SUCCESS;
	_willplay = (int) calc_f$symbol(&p_global->formula, "f$willplay", &e);
	e = SUCCESS;
	_wontplay = (int) calc_f$symbol(&p_global->formula, "f$wontplay", &e);
	e = SUCCESS;
	_mustplay = (int) calc_f$symbol(&p_global->formula, "f$mustplay", &e);
	e = SUCCESS;
	_topclip = (int) calc_f$symbol(&p_global->formula, "f$topclip", &e);

	// Call prw1326 callback if needed
	if (p_symbols->prw1326()->useme==1326 && 
		p_symbols->prw1326()->usecallback==1326 && 
		(p_symbols->sym()->br!=1 || p_symbols->prw1326()->preflop==1326) )
	{
		p_symbols->prw1326()->prw_callback(); //Matrix 2008-05-09
	}

	__SEH_LOGFATAL("CIteratorThread::IteratorThreadFunction :\n");
}

int CIteratorThread::InRange(int card1, int card2)
{
	__SEH_HEADER

	//returns non-zero if card pair acceptable for prwin calculation

	/*
	p |---|	---------
	r |   |   /		 \
	o |   |  /		   \
	b |   | /			 \
	a |   |/			   \
	b |					 \
	l -----------------------------------------------------
	e	 1	2	   3	 4	 handrank169 ->
	1=mustplay
	2=topclip
	3=willplay
	4=wontplay
	(with the current statistics available in OH only willplay and wontplay should be used.
	 mustplay and topclip are debatable constructs at the best of times 2008-01-29)
	*/

	extern int willplay, wontplay, topclip, mustplay;
	extern int pair2ranko[170], pair2ranks[170];
	int i, j, hrank;
	i = card1%13;
	j = card2%13;

	if (j>i) //normalise the card order
	{
		i=j;
		j=card1%13;
	}
	i=i*13+j; //offset into handrank table

	if (card1/13==card2/13)  //same suit
		hrank=pair2ranks[i]; //suited
	else
		hrank=pair2ranko[i]; //not suited

	if (hrank>=wontplay)return 0; //no good, never play these

	if (hrank<=mustplay) return 1; //OK, assumed opponent will always play these

	if ((hrank>topclip)&&(hrank<=willplay))return 1;//OK, in the 100% region

	//we now handle the probability slopes
	if (hrank<=topclip) //handle the good cards not likely to be one-betted with
	{
		j=topclip-rand()%topclip;
		if (hrank>j)return 1; //OK
		else return 0; //nogood
	}

	//now finish with poorish hands of reduced probability
	j=wontplay-willplay;
	if (j<3)
		return 1; //protect ourselves from effect of bad wontplay

	j=willplay+rand()%j;

	if (hrank<j)
		return 1; //OK

	return 0; //no good

	__SEH_LOGFATAL("CIteratorThread::inrange :\n");
}
