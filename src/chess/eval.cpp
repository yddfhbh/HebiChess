#include "chess/eval.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace hebichess {
namespace {
Square at(int f, int r) noexcept { return Square::from_file_rank(static_cast<std::uint8_t>(f), static_cast<std::uint8_t>(r)); }
int signed_score(int w, int b, Color p) noexcept { return p == Color::White ? w - b : b - w; }
bool occupied_by(const Board& b, int f, int r, Color c) noexcept { Square s = at(f, r); return s.is_valid() && b.piece_at(s).color == c && !b.piece_at(s).is_empty(); }

bool attacks(const Board& b, Square from, Square to) noexcept {
  const Piece p = b.piece_at(from); const int df = int(to.file()) - int(from.file()); const int dr = int(to.rank()) - int(from.rank());
  if (p.type == PieceType::Pawn) return dr == (p.color == Color::White ? 1 : -1) && std::abs(df) == 1;
  if (p.type == PieceType::Knight) return df * df + dr * dr == 5;
  if (p.type == PieceType::King) return std::max(std::abs(df), std::abs(dr)) == 1;
  const bool diag = std::abs(df) == std::abs(dr) && df != 0; const bool line = (df == 0) != (dr == 0);
  if ((p.type == PieceType::Bishop && !diag) || (p.type == PieceType::Rook && !line) || (p.type == PieceType::Queen && !diag && !line)) return false;
  const int sf = (df > 0) - (df < 0), sr = (dr > 0) - (dr < 0);
  for (int f = from.file() + sf, r = from.rank() + sr; f != to.file() || r != to.rank(); f += sf, r += sr) if (!b.piece_at(at(f, r)).is_empty()) return false;
  return true;
}
bool attacked(const Board& b, Square s, Color c) noexcept { return b.is_square_attacked(s, c); }
int pst(PieceType t, int f, int r, Color c, int phase) noexcept {
  if (c == Color::Black) r = 7 - r;
  const int center = 6 - std::abs(2 * f - 7) - std::abs(2 * r - 7);
  switch (t) {
    case PieceType::Pawn: return (r * r * 2) + (f >= 2 && f <= 5 ? 5 : 0) - (r <= 1 && (f == 0 || f == 7) ? 3 : 0);
    case PieceType::Knight: return center * 3 - (f == 0 || f == 7 ? 12 : 0) - (r == 0 || r == 7 ? 4 : 0);
    case PieceType::Bishop: return center * 2;
    case PieceType::Rook: return (r == 6 ? 18 : 0) + (f == 0 || f == 7 ? 0 : 2);
    case PieceType::Queen: return center - (r < 2 ? 4 : 0);
    case PieceType::King: return phase < 10 ? center * 2 : -center;
    default: return 0;
  }
}
int count_type(const Board& b, Color c, PieceType t) { int n = 0; for (Piece p : b.squares()) if (p.color == c && p.type == t) ++n; return n; }
int mobility_for(const Board& b, Color c) noexcept {
  int total = 0; constexpr int dirs[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
  constexpr int knight_dirs[8][2]={{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
  for (std::uint8_t i=0;i<64;++i) { Square s=Square::from_index(i); Piece p=b.piece_at(s); if(p.color!=c||p.is_empty()) continue;
    if(p.type==PieceType::Knight) { for(const auto& d:knight_dirs) { Square t=at(s.file()+d[0],s.rank()+d[1]); if(t.is_valid()&&(b.piece_at(t).is_empty()||b.piece_at(t).color!=c)) ++total; } }
    else if(p.type==PieceType::Bishop||p.type==PieceType::Rook||p.type==PieceType::Queen) { int begin=p.type==PieceType::Bishop?4:0,end=p.type==PieceType::Rook?4:8; for(int k=begin;k<end;++k) for(int f=s.file()+dirs[k][0],r=s.rank()+dirs[k][1];at(f,r).is_valid();f+=dirs[k][0],r+=dirs[k][1]) { Piece q=b.piece_at(at(f,r)); if(q.is_empty()) ++total; else {if(q.color!=c) ++total;break;} } }
  } return total;
}
int pressure_for(const Board& b, Color c) noexcept {
  Square k=b.find_king(opposite(c)); if(!k.is_valid()) return 0; int attackers=0, units=0; constexpr int w[]={0,2,6,5,7,10,3};
  for(std::uint8_t i=0;i<64;++i){Square s=Square::from_index(i);Piece p=b.piece_at(s);if(p.color!=c||p.is_empty())continue;bool hit=false;for(int df=-1;df<=1;++df)for(int dr=-1;dr<=1;++dr){Square z=at(k.file()+df,k.rank()+dr);if(z.is_valid()&&attacks(b,s,z)){hit=true;break;}}if(hit){++attackers;units+=w[int(p.type)];}}
  const int mult = attackers <= 1 ? 1 : attackers == 2 ? 12 : attackers == 3 ? 16 : 20;
  return units * mult / 10 + (attackers >= 2 ? attackers * 3 : 0);
}
int shield(const Board& b, Color c) noexcept { Square k=b.find_king(c); if(!k.is_valid())return -40; int score=0; int dir=c==Color::White?1:-1; for(int f=std::max(0,int(k.file())-1);f<=std::min(7,int(k.file())+1);++f){int r=int(k.rank())+dir; if(occupied_by(b,f,r,c))score+=8; else if(occupied_by(b,f,r+dir,c))score+=3; else score-=10;} return score; }
int open_lines(const Board& b, Color c) noexcept { Square k=b.find_king(c); if(!k.is_valid())return -20; int score=0; for(int f=0;f<8;++f){bool own=false,enemy=false;for(int r=0;r<8;++r){Piece p=b.piece_at(at(f,r));if(p.type==PieceType::Pawn){if(p.color==c)own=true;else enemy=true;}}if(!own){if(!enemy)score-=4; for(int r=0;r<8;++r){Piece p=b.piece_at(at(f,r));if(p.color!=c&&(p.type==PieceType::Rook||p.type==PieceType::Queen)&&std::abs(r-int(k.rank()))<8)score-=6;}}} return score; }
} // namespace

int piece_value(PieceType t) noexcept { switch(t){case PieceType::Pawn:return 100;case PieceType::Knight:return 320;case PieceType::Bishop:return 330;case PieceType::Rook:return 500;case PieceType::Queen:return 900;default:return 0;} }
int game_phase(const Board& b) noexcept { int p=0; for(Piece x:b.squares()) p += x.type==PieceType::Queen?4:x.type==PieceType::Rook?2:(x.type==PieceType::Bishop||x.type==PieceType::Knight); return std::min(24,p); }
int evaluate_material(const Board& b, Color p) noexcept {int w=0,bl=0;for(Piece x:b.squares())(x.color==Color::White?w:bl)+=piece_value(x.type);return signed_score(w,bl,p);}
int evaluate_piece_square(const Board& b, Color p) noexcept {int w=0,bl=0,ph=game_phase(b);for(std::uint8_t i=0;i<64;++i){Square s=Square::from_index(i);Piece x=b.piece_at(s);if(!x.is_empty())(x.color==Color::White?w:bl)+=pst(x.type,s.file(),s.rank(),x.color,ph);}return signed_score(w,bl,p);}
int evaluate_piece_activity(const Board& b, Color p) noexcept { return evaluate_piece_square(b,p); }
int evaluate_mobility(const Board& b, Color p) noexcept {return signed_score(mobility_for(b,Color::White)*2,mobility_for(b,Color::Black)*2,p);}
int evaluate_pawn_structure(const Board& b, Color p) noexcept {int score[2]={};for(int ci=0;ci<2;++ci){Color c=ci?Color::Black:Color::White;for(int f=0;f<8;++f){int n=0;for(int r=0;r<8;++r)n+=occupied_by(b,f,r,c)&&b.piece_at(at(f,r)).type==PieceType::Pawn;if(n>1)score[ci]-=(n-1)*10;if(n){bool left=false,right=false;for(int r=0;r<8;++r){left|=occupied_by(b,f-1,r,c)&&b.piece_at(at(f-1,r)).type==PieceType::Pawn;right|=occupied_by(b,f+1,r,c)&&b.piece_at(at(f+1,r)).type==PieceType::Pawn;}if(!left&&!right)score[ci]-=12;}}}for(int i=0;i<64;++i){Square s=Square::from_index(i);Piece x=b.piece_at(s);if(x.type==PieceType::Pawn){for(int df:{-1,1}){Square q=at(s.file()+df,s.rank());if(q.is_valid()&&b.piece_at(q)==Piece{PieceType::Pawn,x.color})score[x.color==Color::White?0:1]+=4;}}}return signed_score(score[0],score[1],p);}
int evaluate_passed_pawns(const Board& b, Color p) noexcept {int score[2]={};for(int i=0;i<64;++i){Square s=Square::from_index(i);Piece x=b.piece_at(s);if(x.type!=PieceType::Pawn)continue;Color e=opposite(x.color);bool passed=true;int dir=x.color==Color::White?1:-1;for(int f=std::max(0,int(s.file())-1);f<=std::min(7,int(s.file())+1);++f)for(int r=int(s.rank())+dir;r>=0&&r<8;r+=dir)if(occupied_by(b,f,r,e)&&b.piece_at(at(f,r)).type==PieceType::Pawn)passed=false;if(passed){int rank=x.color==Color::White?s.rank():7-s.rank();score[x.color==Color::White?0:1]+=rank*rank+rank*3;}}return signed_score(score[0],score[1],p);}
int evaluate_rooks(const Board& b, Color p) noexcept {int score[2]={};for(int ci=0;ci<2;++ci){Color c=ci?Color::Black:Color::White;for(int i=0;i<64;++i){Square s=Square::from_index(i);Piece x=b.piece_at(s);if(x.color!=c||x.type!=PieceType::Rook)continue;int f=s.file();bool own=false,enemy=false;for(int r=0;r<8;++r){Piece q=b.piece_at(at(f,r));if(q.type==PieceType::Pawn)(q.color==c?own:enemy)=true;}score[ci]+=own?0:enemy?10:18;if((c==Color::White?s.rank():7-s.rank())==6)score[ci]+=14;}}return signed_score(score[0],score[1],p);}
int evaluate_king_safety(const Board& b, Color p) noexcept {int w=shield(b,Color::White)+open_lines(b,Color::White),bl=shield(b,Color::Black)+open_lines(b,Color::Black);int ph=game_phase(b);return signed_score(w*std::max(5,ph)/24,bl*std::max(5,ph)/24,p);}
int evaluate_king_attack(const Board& b, Color p) noexcept {return signed_score(pressure_for(b,Color::White),pressure_for(b,Color::Black),p);}
int evaluate_attack_pressure(const Board& b, Color p) noexcept {return evaluate_king_attack(b,p);}
int evaluate_space(const Board& b, Color p) noexcept {int s[2]={};for(int i=0;i<64;++i){Square q=Square::from_index(i);for(int ci=0;ci<2;++ci){Color c=ci?Color::Black:Color::White;if(attacked(b,q,c)&&((c==Color::White&&q.rank()>=4)||(c==Color::Black&&q.rank()<=3)))s[ci]+= (q.file()>=2&&q.file()<=5?2:1);}}return signed_score(s[0],s[1],p);}
int evaluate_threats(const Board& b, Color p) noexcept {int s[2]={};for(int i=0;i<64;++i){Square q=Square::from_index(i);Piece x=b.piece_at(q);if(x.is_empty()||x.type==PieceType::Pawn||x.type==PieceType::King)continue;if(attacked(b,q,opposite(x.color))&&!attacked(b,q,x.color))s[x.color==Color::White?1:0]+=piece_value(x.type)/40;}return signed_score(s[0],s[1],p);}
int evaluate_initiative(const Board& b, Color p) noexcept {int w= b.side_to_move()==Color::White?10:-10;return p==Color::White?w:-w;}
EvalBreakdown evaluate_breakdown(const Board& b, Color p) noexcept {EvalBreakdown e;e.material=evaluate_material(b,p);e.pst=evaluate_piece_square(b,p);e.mobility=evaluate_mobility(b,p);e.pawns=evaluate_pawn_structure(b,p);e.passed_pawns=evaluate_passed_pawns(b,p);e.bishop_pair=signed_score(count_type(b,Color::White,PieceType::Bishop)>=2?30:0,count_type(b,Color::Black,PieceType::Bishop)>=2?30:0,p);e.rook_activity=evaluate_rooks(b,p);e.king_safety=evaluate_king_safety(b,p);e.king_attack=evaluate_king_attack(b,p);e.space=evaluate_space(b,p);e.threats=evaluate_threats(b,p);e.initiative=evaluate_initiative(b,p);e.total=e.material+e.pst+e.mobility+e.pawns+e.passed_pawns+e.bishop_pair+e.rook_activity+e.king_safety+e.king_attack+e.space+e.threats+e.initiative;return e;}
int evaluate(const Board& b) noexcept {return evaluate_breakdown(b,b.side_to_move()).total;}
} // namespace hebichess
