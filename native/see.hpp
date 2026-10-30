#pragma once
// Local swap-off evaluation. Recompute x-rays and reject pinned recaptures and
// illegal king captures. This is material analysis, not a tactical search.
inline int static_exchange(const chess::Board& board, chess::Move move) {
    using namespace chess;
    if(move.typeOf()==Move::CASTLING) return 0;
    uint64_t pieces[2][6]{};
    for(int c=0;c<2;++c) for(int t=0;t<6;++t)
        pieces[c][t]=board.pieces(PieceType(static_cast<PieceType::underlying>(t)),Color(c)).getBits();
    auto price=[](int t){return t==5?20000:values[t];};
    int side=int(board.sideToMove()),from=move.from().index(),to=move.to().index();
    int moving=int(board.at<PieceType>(move.from()));
    int victim=move.typeOf()==Move::ENPASSANT?0:int(board.at<PieceType>(move.to()));
    int captured=move.typeOf()==Move::ENPASSANT?to+(side==0?-8:8):to;
    int promoted=move.typeOf()==Move::PROMOTION?int(move.promotionType()):moving;
    int gains[33]{}; gains[0]=(victim<6?price(victim):0)+price(promoted)-price(moving);
    pieces[side][moving]&=~(1ULL<<from);
    if(victim<6) pieces[side^1][victim]&=~(1ULL<<captured);
    pieces[side][promoted]|=1ULL<<to;
    uint64_t occupied=board.occ().getBits()&~(1ULL<<from);
    if(move.typeOf()==Move::ENPASSANT) occupied&=~(1ULL<<captured);
    occupied|=1ULL<<to;
    auto attackers=[&](int c,int square,uint64_t occ){
        Square sq(square);
        return ((attacks::pawn(~Color(c),sq).getBits()&pieces[c][0]) |
                (attacks::knight(sq).getBits()&pieces[c][1]) |
                (attacks::bishop(sq,Bitboard(occ)).getBits()&(pieces[c][2]|pieces[c][4])) |
                (attacks::rook(sq,Bitboard(occ)).getBits()&(pieces[c][3]|pieces[c][4])) |
                (attacks::king(sq).getBits()&pieces[c][5])) & occ;
    };
    int occupant=promoted,owner=side,depth=0; side^=1;
    while(depth<32&&occupant!=5) {
        uint64_t candidates=attackers(side,to,occupied);
        bool found=false; int selected=-1,nextType=-1,selectedSq=-1;
        for(int t=0;t<6&&!found;++t) {
            uint64_t options=candidates&pieces[side][t];
            while(options) {
                int sq=__builtin_ctzll(options); options&=options-1;
                int next=(t==0&&(to/8==0||to/8==7))?4:t;
                pieces[owner][occupant]&=~(1ULL<<to);
                pieces[side][t]&=~(1ULL<<sq); pieces[side][next]|=1ULL<<to;
                uint64_t occ=occupied&~(1ULL<<sq);
                int king=__builtin_ctzll(pieces[side][5]);
                bool safe=!attackers(side^1,king,occ);
                pieces[side][next]&=~(1ULL<<to); pieces[side][t]|=1ULL<<sq;
                pieces[owner][occupant]|=1ULL<<to;
                if(safe) {selected=t;nextType=next;selectedSq=sq;found=true;break;}
            }
        }
        if(!found) break;
        ++depth; gains[depth]=price(occupant)+price(nextType)-price(selected)-gains[depth-1];
        pieces[owner][occupant]&=~(1ULL<<to);
        pieces[side][selected]&=~(1ULL<<selectedSq);pieces[side][nextType]|=1ULL<<to;
        occupied&=~(1ULL<<selectedSq);owner=side;occupant=nextType;side^=1;
    }
    while(depth>0){gains[depth-1]=-std::max(-gains[depth-1],gains[depth]);--depth;}
    return gains[0];
}
